#include "stdafx.h"
#pragma hdrstop

#include "xrDebug.h"
#include "Debug/StackTrace.h"
#include "os_clipboard.h"
#include "log.h"
#include "Threading/ScopeLock.hpp"

#include <SDL.h>

#include <csignal>

#if defined(XR_PLATFORM_WINDOWS)
#   include <dbghelp.h>
#   include <direct.h>
#   include <new.h> // for _set_new_mode
#   include <errorrep.h> // ReportFault

#   define USE_BUG_TRAP
#   ifdef USE_BUG_TRAP
#       include "BugTrap.h"
#   endif

#   include "Debug/dxerr.h"
#endif

#if defined(XR_PLATFORM_LINUX) || defined(XR_PLATFORM_APPLE) || defined(XR_PLATFORM_BSD)
#   if __has_include(<sys/ptrace.h>)
#       include <sys/ptrace.h>
#       define PTRACE_AVAILABLE

#       if defined(XR_PLATFORM_APPLE) || defined(XR_PLATFORM_BSD)
#           define PTRACE_TRACEME PT_TRACE_ME
#           define PTRACE_DETACH PT_DETACH
#       endif
#   endif
#   include <unistd.h>
#   include <execinfo.h>
#   include <sys/time.h>
#   include <time.h>
#   include <atomic>
#   include <thread>
#   include "xrCore/Threading/ThreadUtil.h"
#endif

constexpr SDL_MessageBoxButtonData buttons[] =
{
    /* .flags, .buttonid, .text */
    { 0, (int)AssertionResult::ignore, "Continue"  },
    { 0, (int)AssertionResult::tryAgain, "Try again" },

    { SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT |
      SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT,
         (int)AssertionResult::abort, "Cancel" }
};

AssertionResult xrDebug::ShowMessage(pcstr title, pcstr message, bool simpleMode)
{
#ifdef XR_PLATFORM_WINDOWS // because Windows default Message box is fancy
    HWND hwnd = nullptr;
    if (windowHandler)
        hwnd = static_cast<HWND>(windowHandler->GetApplicationWindowHandle());

    if (simpleMode)
    {
        MessageBox(hwnd, message, title, MB_OK | MB_ICONERROR | MB_SYSTEMMODAL);
        return AssertionResult::ok;
    }

    const int result = MessageBox(hwnd, message, title,
        MB_CANCELTRYCONTINUE | MB_ICONERROR | MB_SYSTEMMODAL);

    switch (result)
    {
    case IDCANCEL: return AssertionResult::abort;
    case IDTRYAGAIN: return AssertionResult::tryAgain;
    case IDCONTINUE: return AssertionResult::ignore;
    default: return AssertionResult::undefined;
    }
#else
    if (simpleMode)
    {
        SDL_Window* parent = windowHandler ? windowHandler->GetApplicationWindow() : nullptr;
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title, message, parent);
        return AssertionResult::ok;
    }

    SDL_MessageBoxData data =
    {
        SDL_MESSAGEBOX_ERROR,
        windowHandler ? windowHandler->GetApplicationWindow() : nullptr,
        title, message, SDL_arraysize(buttons), buttons, nullptr
    };

    int button = -1;
    SDL_ShowMessageBox(&data, &button);
    return (AssertionResult)button;
#endif
}

SDL_AssertState SDLAssertionHandler(const SDL_AssertData* data,
    void* /*userdata*/)
{
    if (data->always_ignore)
        return SDL_ASSERTION_ALWAYS_IGNORE;

    static constexpr pcstr desc = "SDL2 assertion triggered";
    bool alwaysIgnore = false;

    const auto result = xrDebug::Fail(alwaysIgnore,
        { data->filename, data->linenum, data->function },
        data->condition, desc);

    switch (result)
    {
    case AssertionResult::ignore:
        return SDL_ASSERTION_ALWAYS_IGNORE;

    case AssertionResult::tryAgain:
        return SDL_ASSERTION_RETRY;

    case AssertionResult::abort:
        return SDL_ASSERTION_ABORT;

    case AssertionResult::undefined:
    case AssertionResult::ok:
    default:
        return SDL_ASSERTION_IGNORE;
    }
}

IWindowHandler* xrDebug::windowHandler = nullptr;
IUserConfigHandler* xrDebug::userConfigHandler = nullptr;
xrDebug::UnhandledExceptionFilter xrDebug::PrevFilter = nullptr;
xrDebug::OutOfMemoryCallbackFunc xrDebug::OutOfMemoryCallback = nullptr;
string_path xrDebug::BugReportFile;
bool xrDebug::ErrorAfterDialog = false;
bool xrDebug::ShowErrorMessage = true;

// Renderer playground (epic #12) GL error sink. nullptr by default — set
// by xrEngine when the playground initialises, called from the Apple-side
// CHK_GL macro after the existing Msg() so the playground can show recent
// errors in its Event Log tab without scraping the log file.
XRCORE_API xr_gl_error_sink_fn xr_gl_error_sink = nullptr;

#ifdef PROFILE_CRITICAL_SECTIONS
Lock xrDebug::failLock(MUTEX_PROFILE_ID(xrDebug::Backend));
#else
Lock xrDebug::failLock;
#endif

void xrDebug::SetBugReportFile(const char* fileName) { xr_strcpy(BugReportFile, fileName); }

void xrDebug::LogStackTrace(const char* header)
{
    xr_vector<xr_string> stackTrace = BuildStackTrace();
    Msg("%s", header);
    for (const auto& frame : stackTrace)
    {
        Msg("%s", frame.c_str());
    }
}


namespace
{
// Safe printf-into-buffer that handles vsnprintf's "would-have-been" return
// value correctly. The naive `buffer += xr_sprintf(buffer, end - buffer, ...)`
// pattern advances `buffer` past `end` whenever a single call truncates
// (vsnprintf returns the length it *would* have written, not what it wrote).
// On the next call `end - buffer` then underflows as size_t to a huge value,
// the bounded-write becomes unbounded, and the stack-allocated assertionInfo
// gets clobbered -- including the saved return address. On ARM64 macOS this
// trips the PAC trap on Fail()'s return; on Windows it's a latent bug masked
// by the BugTrap short-circuit.
void safe_append(char*& buffer, const char* const oneAboveBuffer, const char* fmt, ...)
{
    if (!buffer || buffer >= oneAboveBuffer)
        return;
    const size_t remaining = static_cast<size_t>(oneAboveBuffer - buffer);
    if (remaining < 2) // need room for at least one char + NUL
        return;
    va_list args;
    va_start(args, fmt);
    const int written = vsnprintf(buffer, remaining, fmt, args);
    va_end(args);
    if (written < 0)
        return;
    const size_t actuallyWritten = (static_cast<size_t>(written) < remaining)
        ? static_cast<size_t>(written)
        : remaining - 1; // truncated; advance only by what was actually written
    buffer += actuallyWritten;
}
} // namespace

void xrDebug::GatherInfo(char* assertionInfo, size_t bufferSize, const ErrorLocation& loc, const char* expr,
                         const char* desc, const char* arg1, const char* arg2)
{
    char* buffer = assertionInfo;
    if (!expr)
        expr = "<no expression>";
    bool extendedDesc = desc && strchr(desc, '\n');
    pcstr prefix = "[error] ";
    const char* const oneAboveBuffer = assertionInfo + bufferSize;
    safe_append(buffer, oneAboveBuffer, "\nFATAL ERROR\n\n");
    safe_append(buffer, oneAboveBuffer, "%sExpression    : %s\n", prefix, expr);
    safe_append(buffer, oneAboveBuffer, "%sFunction      : %s\n", prefix, loc.Function);
    safe_append(buffer, oneAboveBuffer, "%sFile          : %s\n", prefix, loc.File);
    safe_append(buffer, oneAboveBuffer, "%sLine          : %d\n", prefix, loc.Line);
    if (extendedDesc)
    {
        safe_append(buffer, oneAboveBuffer, "\n%s\n", desc);
        if (arg1)
        {
            safe_append(buffer, oneAboveBuffer, "%s\n", arg1);
            if (arg2)
                safe_append(buffer, oneAboveBuffer, "%s\n", arg2);
        }
    }
    else
    {
        safe_append(buffer, oneAboveBuffer, "%sDescription   : %s\n", prefix, desc);
        if (arg1)
        {
            if (arg2)
            {
                safe_append(buffer, oneAboveBuffer, "%sArgument 0    : %s\n", prefix, arg1);
                safe_append(buffer, oneAboveBuffer, "%sArgument 1    : %s\n", prefix, arg2);
            }
            else
                safe_append(buffer, oneAboveBuffer, "%sArguments     : %s\n", prefix, arg1);
        }
    }
    safe_append(buffer, oneAboveBuffer, "\n");

    Log(assertionInfo);
    FlushLog();

    buffer = assertionInfo;
#if defined(XR_PLATFORM_WINDOWS)
    if (DebuggerIsPresent() || !strstr(GetCommandLine(), "-no_call_stack_assert"))
        return;
#endif

    Log("stack trace:\n");
    safe_append(buffer, oneAboveBuffer, "stack trace:\n\n");

    xr_vector<xr_string> stackTrace = BuildStackTrace();
    for (size_t i = 2; i < stackTrace.size(); i++)
    {
        Log(stackTrace[i].c_str());
        safe_append(buffer, oneAboveBuffer, "%s\n", stackTrace[i].c_str());
    }

    FlushLog();
    os_clipboard::copy_to_clipboard(assertionInfo);
}

void xrDebug::Fatal(const ErrorLocation& loc, const char* format, ...)
{
    string1024 desc;
    va_list args;
    va_start(args, format);
    vsnprintf(desc, sizeof(desc), format, args);
    va_end(args);
    bool ignoreAlways = true;
    Fail(ignoreAlways, loc, nullptr, "fatal error", desc);
}

AssertionResult xrDebug::Fail(bool& ignoreAlways, const ErrorLocation& loc, const char* expr, long hresult, const char* arg1,
                   const char* arg2)
{
    return Fail(ignoreAlways, loc, expr, xrDebug::ErrorToString(hresult), arg1, arg2);
}

AssertionResult xrDebug::Fail(bool& ignoreAlways, const ErrorLocation& loc, const char* expr, const char* desc, const char* arg1,
                   const char* arg2)
{
    ScopeLock lock(&failLock);

    if (windowHandler)
        windowHandler->OnErrorDialog(true); // Call it only after locking so that multiple threads won't call this function simultaneously.

    ErrorAfterDialog = true;
    string4096 assertionInfo;
    GatherInfo(assertionInfo, sizeof(assertionInfo), loc, expr, desc, arg1, arg2);

    if (ShowErrorMessage)
    {
        xr_strcat(assertionInfo,
            "\r\n"
            "Press CANCEL to abort execution\r\n"
            "Press TRY AGAIN to continue execution\r\n"
            "Press CONTINUE to continue execution and ignore all the errors of this type\r\n"
            "\r\n");
    }

    FlushLog();

    bool resetFullscreen = false;
    AssertionResult result = AssertionResult::abort;
    if (Core.PluginMode)
        /*result =*/ ShowMessage("X-Ray error", assertionInfo); // Do not assign 'result'
    else
    {
        if (ShowErrorMessage)
            result = ShowMessage("Fatal error", assertionInfo, false);

        switch (result)
        {
        case AssertionResult::tryAgain:
            ErrorAfterDialog = false;
            resetFullscreen = windowHandler != nullptr;
            break;

        case AssertionResult::ignore:
            ErrorAfterDialog = false;
            ignoreAlways = true;
            resetFullscreen = windowHandler != nullptr;
            break;

        case AssertionResult::undefined:
            xr_strcat(assertionInfo, SDL_GetError());
            [[fallthrough]];
        case AssertionResult::abort:
            [[fallthrough]];
        default:
#ifdef USE_BUG_TRAP
            BT_SetUserMessage(assertionInfo);
#endif
            // calling DEBUG_BREAK with no debugger will trigger BugTrap
            // we must hide the window
            if (windowHandler && !DebuggerIsPresent())
                windowHandler->OnFatalError();
            DEBUG_BREAK;
        } // switch (result)
    }

    if (resetFullscreen)
        windowHandler->OnErrorDialog(false); // Call it only before unlocking so that multiple threads won't call this function simultaneously.

    return result;
}

AssertionResult xrDebug::Fail(bool& ignoreAlways, const ErrorLocation& loc, const char* expr, const std::string& desc,
                   const char* arg1, const char* arg2)
{
    return Fail(ignoreAlways, loc, expr, desc.c_str(), arg1, arg2);
}

[[noreturn]]
void xrDebug::DoExit(const std::string& message)
{
    ScopeLock lock(&failLock);

    if (windowHandler)
        windowHandler->OnErrorDialog(true);

    FlushLog();

    if (ShowErrorMessage)
    {
        const auto result = ShowMessage(Core.ApplicationName, message.c_str(), false);
        if (result != AssertionResult::abort && DebuggerIsPresent())
            DEBUG_BREAK;
    }
    else
        ShowMessage(Core.ApplicationName, message.c_str());

    if (windowHandler)
        windowHandler->OnFatalError();

#if defined(XR_PLATFORM_WINDOWS)
    TerminateProcess(GetCurrentProcess(), 1);
#else
    exit(1);
#endif
    // if you're under debugger, you can jump here manually
    if (windowHandler)
        windowHandler->OnErrorDialog(false);
}

pcstr xrDebug::ErrorToString(long code)
{
    const char* result = nullptr;
#if defined(XR_PLATFORM_WINDOWS)
    static string1024 descStorage;
    DXGetErrorDescription(code, descStorage, sizeof(descStorage));
    if (!result)
    {
        FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, 0, code, 0, descStorage, sizeof(descStorage) - 1, 0);
        result = descStorage;
    }
#else
    // No symbolic translator on non-Windows yet. Returning nullptr was unsafe
    // because GatherInfo passes desc into strchr(desc, '\n') unconditionally;
    // a non-null placeholder lets the failure path render an honest message
    // instead of triggering UB en route to the FATAL dialog.
    (void)code;
    result = "<error code translation unavailable on this platform>";
#endif
    return result;
}

int out_of_memory_handler(size_t size)
{
    xrDebug::OutOfMemoryCallbackFunc cb = xrDebug::GetOutOfMemoryCallback();
    if (cb)
        cb();
    else
    {
        Memory.mem_compact();
        const size_t processHeap = Memory.mem_usage();
        const auto [ecoStringsBytes, ecoStringsCount] = g_pStringContainer->stat_economy();
        const size_t ecoSmem = g_pSharedMemoryContainer->stat_economy();
        Msg("* [x-ray]: process heap[%zu K]", processHeap / 1024);
        Msg("* [x-ray]: shared strings: memory[%ld K], count[%lu]", ecoStringsBytes / 1024, ecoStringsCount);
        Msg("* [x-ray]: shared memory[%ld K]", ecoSmem);
    }
    xrDebug::Fatal(DEBUG_INFO, "Out of memory. Memory request: %zu K", size / 1024);
    return 1;
}

extern pcstr log_name();

void WINAPI xrDebug::PreErrorHandler(INT_PTR)
{
#if defined(USE_BUG_TRAP) && defined(XR_PLATFORM_WINDOWS)
    if (xr_FS && FS.m_Flags.test(CLocatorAPI::flReady))
    {
        string_path cfg_full_name;
        __try
        {
            // Code below copied from CCC_LoadCFG::Execute (xr_ioc_cmd.cpp)
            // XXX: Refactor Console to accept user config filename on initialization or even construction!
            // XXX: Maybe refactor CCC_LoadCFG, move code for loading user.ltx into a generic function
            const auto cfg_name = userConfigHandler ? userConfigHandler->GetUserConfigFileName() : "user.ltx";
            FS.update_path(cfg_full_name, "$app_data_root$", cfg_name);

            if (!FS.exist(cfg_full_name))
                FS.update_path(cfg_full_name, "$fs_root$", cfg_name);

            if (!FS.exist(cfg_full_name))
                xr_strcpy(cfg_full_name, cfg_name);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            xr_strcpy(cfg_full_name, "user.ltx");
        }
        BT_AddLogFile(cfg_full_name);
    }

    if (*BugReportFile)
        BT_AddLogFile(BugReportFile);

    BT_SaveSnapshot(nullptr);
#endif
}

void xrDebug::SetupExceptionHandler()
{
#if defined(USE_BUG_TRAP) && defined(XR_PLATFORM_WINDOWS)
    const auto commandLine = GetCommandLine();

    // disable 'appname has stopped working' popup dialog
    const auto prevMode = SetErrorMode(SEM_NOGPFAULTERRORBOX);
    SetErrorMode(prevMode | SEM_NOGPFAULTERRORBOX);
    BT_InstallSehFilter();

    if (!GEnv.isDedicatedServer && !strstr(commandLine, "-silent_error_mode"))
        BT_SetActivityType(BTA_SHOWUI);
    else
        BT_SetActivityType(BTA_SAVEREPORT);
    BT_SetDialogMessage(BTDM_INTRO2,
                        "This is OpenXRay crash reporting client. "
                        "To help the development process, "
                        "please Submit Bug or save report and email it manually (button More...)."
                        "\r\n"
                        "Many thanks in advance and sorry for the inconvenience.");
    BT_SetPreErrHandler(PreErrorHandler, 0);
    BT_SetAppName("OpenXRay");
    BT_SetReportFormat(BTRF_TEXT);
    BT_SetFlags(BTF_DETAILEDMODE | BTF_ATTACHREPORT);

    auto minidumpFlags = MiniDumpWithDataSegs|
        MiniDumpWithIndirectlyReferencedMemory |
        MiniDumpScanMemory |
        MiniDumpWithProcessThreadData |
        MiniDumpWithThreadInfo;

    if (strstr(commandLine, "-full_memory_dump"))
        minidumpFlags |= MiniDumpWithFullMemory | MiniDumpIgnoreInaccessibleMemory;
    else if (strstr(commandLine, "-detailed_minidump"))
        minidumpFlags |= MiniDumpWithIndirectlyReferencedMemory;

    BT_SetDumpType(minidumpFlags);
    //BT_SetSupportEMail("cop-crash-report@stalker-game.com");
    BT_SetSupportEMail("openxray@yahoo.com");
    BT_SetSupportURL("https://github.com/OpenXRay/xray-16/issues");
#endif
}

void xrDebug::OnFilesystemInitialized()
{
#ifdef USE_BUG_TRAP
    string_path path{};
    FS.update_path(path, "$logs$", "", false);
    if (!path[0] || path[0] != _DELIMITER && path[1] != ':') // relative path
    {
        string_path currentDir;
        _getcwd(currentDir, sizeof(currentDir));
        string_path relDir;
        xr_strcpy(relDir, path);
        strconcat(path, currentDir, DELIMITER, relDir);
    }
    xr_strcat(path, log_name());
    BT_AddLogFile(path);

    if (FS.update_path(path, "$app_data_root$", "reports", false))
    {
        BT_SetReportFilePath(path);
    }
#endif
}

bool xrDebug::DebuggerIsPresent()
{
#ifdef XR_PLATFORM_WINDOWS
    return IsDebuggerPresent();
#elif defined(PTRACE_AVAILABLE)
    if (ptrace(PTRACE_TRACEME, 0, 0, 0) == -1)
        return true;
    ptrace(PTRACE_DETACH, 0, 0, 0);
    return false;
#else
    return false;
#endif
}

void xrDebug::FormatLastError(char* buffer, const size_t& bufferSize)
{
#if defined(XR_PLATFORM_WINDOWS)
    const int lastErr = GetLastError();
    if (lastErr == ERROR_SUCCESS)
    {
        *buffer = 0;
        return;
    }
    void* msg = nullptr;
    FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM, nullptr, lastErr,
                  MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (pstr)&msg, 0, nullptr);
    // XXX nitrocaster: check buffer overflow
    xr_sprintf(buffer, bufferSize, "[error][%8d]: %s", lastErr, (char*)msg);
    LocalFree(msg);
#endif
}

LONG WINAPI xrDebug::UnhandledFilter(EXCEPTION_POINTERS* exPtrs)
{
#if defined(XR_PLATFORM_WINDOWS)
    ScopeLock lock(&failLock);

    string256 errMsg;
    FormatLastError(errMsg, sizeof(errMsg));
    if (!ErrorAfterDialog && !strstr(GetCommandLine(), "-no_call_stack_assert"))
    {
        CONTEXT save = *exPtrs->ContextRecord;
        xr_vector<xr_string> stackTrace = BuildStackTrace(exPtrs->ContextRecord, 1024);
        *exPtrs->ContextRecord = save;
        Msg("stack trace:\n");
#ifdef DEBUG
        if (!DebuggerIsPresent())
            os_clipboard::copy_to_clipboard("stack trace:\r\n\r\n");
#endif
        string4096 buffer;
        for (size_t i = 0; i < stackTrace.size(); i++)
        {
            Log(stackTrace[i].c_str());
            xr_sprintf(buffer, sizeof(buffer), "%s\r\n", stackTrace[i].c_str());
#ifdef DEBUG
            if (!DebuggerIsPresent())
                os_clipboard::update_clipboard(buffer);
#endif
        }
        if (*errMsg)
        {
            Msg("\n%s", errMsg);
            xr_strcat(errMsg, "\r\n");
#ifdef DEBUG
            if (!DebuggerIsPresent())
                os_clipboard::update_clipboard(buffer);
#endif
        }
    }
    FlushLog();

    if (windowHandler)
        windowHandler->OnErrorDialog(true);

    static constexpr pcstr fatalError = "Fatal error";

    AssertionResult msgRes = AssertionResult::abort;

    if (!ErrorAfterDialog && ShowErrorMessage)
    {
        static constexpr pcstr msg = "Fatal error occurred\n\n"
            "Press OK to abort program execution";
        msgRes = ShowMessage(fatalError, msg);
    }

    BT_SetUserMessage(fatalError);
    BT_SaveSnapshotEx(exPtrs, nullptr);

    const auto reportRes = ReportFault(exPtrs, 0);
    if (msgRes != AssertionResult::abort ||
        reportRes == frrvLaunchDebugger)
    {
        constexpr cpcstr debugger = "Please, attach the debugger to the process"
            " if you want to debug this fatal error.";
        ShowMessage("xrDebug", debugger);
        if (DebuggerIsPresent())
            DEBUG_BREAK;
    }

    // Typically, PrevFilter is BugTrap filter
    if (PrevFilter)
    {
        if (windowHandler)
            windowHandler->OnFatalError();
        PrevFilter(exPtrs);
    }

    if (windowHandler)
        windowHandler->OnErrorDialog(false);

    return EXCEPTION_CONTINUE_SEARCH;
#else
    return 0;
#endif
}

#ifndef USE_BUG_TRAP
[[noreturn]]
void xr_terminate()
{
#if defined(XR_PLATFORM_WINDOWS)
    if (strstr(GetCommandLine(), "-silent_error_mode"))
        exit(-1);
#endif
    //ScopeLock lock(&failLock);

    string4096 assertionInfo;
    xrDebug::GatherInfo(assertionInfo,sizeof(assertionInfo), DEBUG_INFO, nullptr, "Unexpected application termination");
    xr_strcat(assertionInfo, "Press OK to abort execution\r\n");
    xrDebug::ShowMessage("Fatal Error", assertionInfo);
    exit(-1);
}
#endif // USE_BUG_TRAP

#if defined(XR_PLATFORM_LINUX) || defined(XR_PLATFORM_APPLE) || defined(XR_PLATFORM_BSD)
namespace
{
// Re-entry guard for OnFatalSignal. A second fatal signal (e.g. a second
// SIGSEGV inside the backtrace path) must not loop back through us; it
// _exits straight away. exchange-return value: previous signal stored, 0
// means we are the first caller.
std::atomic<int> sigInProgress{0};

// Convert a small non-negative integer into a decimal string written into
// `buf` (size `cap`). Returns number of bytes written (without NUL). Used
// instead of snprintf — snprintf is not strictly async-signal-safe per
// POSIX even though it works in practice on macOS.
size_t async_safe_itoa(int value, char* buf, size_t cap) noexcept
{
    if (cap == 0)
        return 0;
    if (value < 0)
        value = -value;
    // Reverse-fill into a small stack buffer then copy.
    char tmp[16];
    size_t len = 0;
    do
    {
        tmp[len++] = static_cast<char>('0' + (value % 10));
        value /= 10;
    } while (value > 0 && len < sizeof(tmp));
    const size_t out = (len < cap) ? len : cap;
    for (size_t i = 0; i < out; ++i)
        buf[i] = tmp[len - 1 - i];
    return out;
}
} // namespace
#endif

void xrDebug::OnFatalSignal(int sig, const char* reason)
{
#if defined(XR_PLATFORM_LINUX) || defined(XR_PLATFORM_APPLE) || defined(XR_PLATFORM_BSD)
    // Re-entry: if we're already handling a signal, just bail out hard.
    if (sigInProgress.exchange(sig) != 0)
        _exit(128 + sig);

    // Arm a 5-second tripwire. If OnFatalSignal itself deadlocks (the
    // backtrace path can take an internal lock on some platforms) the
    // SIGALRM handler installed in OnThreadSpawn will _exit. ITIMER_REAL
    // fires on wall-clock, not CPU time — survives a wedged thread.
    itimerval tv{};
    tv.it_value.tv_sec = 5;
    setitimer(ITIMER_REAL, &tv, nullptr);

    // Stderr canary. Use fixed strings + manual itoa; no snprintf, no
    // FlushLog, no Cocoa, no xrCore logging — xrCore's log file may be
    // closed by now and any mutex inside Msg/Log is unsafe in a signal.
    static const char prefix[] = "==> FATAL SIGNAL: ";
    ::write(STDERR_FILENO, prefix, sizeof(prefix) - 1);
    char numBuf[16];
    const size_t numLen = async_safe_itoa(sig, numBuf, sizeof(numBuf));
    ::write(STDERR_FILENO, numBuf, numLen);
    if (reason && reason[0])
    {
        ::write(STDERR_FILENO, " (", 2);
        // strlen is async-signal-safe on POSIX (it's pure compute).
        size_t reasonLen = 0;
        while (reason[reasonLen])
            ++reasonLen;
        ::write(STDERR_FILENO, reason, reasonLen);
        ::write(STDERR_FILENO, ")", 1);
    }
    ::write(STDERR_FILENO, "\n", 1);

    // Async-safe backtrace per Apple <execinfo.h>: both backtrace() and
    // backtrace_symbols_fd() are documented as signal-safe.
    void* addrs[64];
    const int n = backtrace(addrs, 64);
    backtrace_symbols_fd(addrs, n, STDERR_FILENO);

    // Skip atexit / static destructors entirely. The crash invariant is
    // unknown; running normal teardown would re-acquire mutexes we likely
    // already own. OS reaps memory, threads, FDs.
    _exit(128 + sig);
#else
    (void)reason;
    _exit(128 + sig);
#endif
}

static void handler_base(int sig, const char* reason)
{
    xrDebug::OnFatalSignal(sig, reason);
}

void xrDebug::StartWatchdog(u32 timeoutSec)
{
#if defined(XR_PLATFORM_LINUX) || defined(XR_PLATFORM_APPLE) || defined(XR_PLATFORM_BSD)
    if (timeoutSec == 0)
        return;

    std::thread([timeoutSec] {
        Threading::SetCurrentThreadName("watchdog");
        timespec ts{};
        clock_gettime(CLOCK_MONOTONIC, &ts);
        u64 lastSeen = g_mainHeartbeat.load(std::memory_order_relaxed);
        u64 lastSeenAtNs = u64(ts.tv_sec) * 1000000000ull + u64(ts.tv_nsec);
        while (true)
        {
            ::sleep(5);
            const u64 current = g_mainHeartbeat.load(std::memory_order_relaxed);
            clock_gettime(CLOCK_MONOTONIC, &ts);
            const u64 nowNs = u64(ts.tv_sec) * 1000000000ull + u64(ts.tv_nsec);
            if (current != lastSeen)
            {
                lastSeen = current;
                lastSeenAtNs = nowNs;
                continue;
            }
            if ((nowNs - lastSeenAtNs) > u64(timeoutSec) * 1000000000ull)
            {
                static const char msg[] = "==> WATCHDOG: main thread stalled, exiting\n";
                ::write(STDERR_FILENO, msg, sizeof(msg) - 1);
                _exit(128 + SIGKILL);
            }
        }
    }).detach();
#else
    (void)timeoutSec;
#endif
}

#if defined(XR_PLATFORM_WINDOWS)
static void invalid_parameter_handler(const wchar_t* expression, const wchar_t* function, const wchar_t* file,
                                      unsigned int line, uintptr_t reserved)
{
    bool ignoreAlways = false;
    string4096 mbExpression;
    string4096 mbFunction;
    string4096 mbFile;
    size_t convertedChars = 0;
    if (expression)
        wcstombs_s(&convertedChars, mbExpression, sizeof(mbExpression), expression, (wcslen(expression) + 1) * 2);
    else
        xr_strcpy(mbExpression, "");
    if (function)
        wcstombs_s(&convertedChars, mbFunction, sizeof(mbFunction), function, (wcslen(function) + 1) * 2);
    else
        xr_strcpy(mbFunction, __FUNCTION__);
    if (file)
        wcstombs_s(&convertedChars, mbFile, sizeof(mbFile), file, (wcslen(file) + 1) * 2);
    else
    {
        line = __LINE__;
        xr_strcpy(mbFile, __FILE__);
    }
    xrDebug::Fail(ignoreAlways, {mbFile, int(line), mbFunction}, mbExpression, "invalid parameter");
}
#endif

void xrDebug::OnThreadSpawn()
{
#ifndef __SANITIZE_ADDRESS__
    std::signal(SIGINT,  nullptr);
    std::signal(SIGILL,  +[](int sig) { handler_base(sig, "illegal instruction"); });
    std::signal(SIGFPE,  +[](int sig) { handler_base(sig, "floating point error"); });
#   ifdef DEBUG
    std::signal(SIGSEGV, +[](int sig) { handler_base(sig, "segmentation fault"); });
#   endif
    std::signal(SIGABRT, +[](int sig) { handler_base(sig, "application is aborting"); });

    // SIGTERM is polite-quit (Force Quit, `kill <pid>`, launchd). It does
    // NOT go through OnFatalSignal — no backtrace, no tripwire, no fancy
    // diagnostics. Just respect the request: write a canary and _exit.
    // Pre-#61 we routed SIGTERM through handler_base → xrDebug::Fail,
    // which acquired failLock and called Cocoa NSAlert. If the main
    // thread already held failLock (e.g. mid-assert) the SIGTERM handler
    // re-entered and deadlocked → unkillable process.
    std::signal(SIGTERM, +[](int sig) {
        static const char msg[] = "==> SIGTERM received, exiting\n";
        ::write(STDERR_FILENO, msg, sizeof(msg) - 1);
        _exit(128 + sig);
    });

    // SIGALRM is the tripwire backstop for OnFatalSignal's setitimer(5s).
    // If we got here, OnFatalSignal itself wedged — last resort _exit.
    std::signal(SIGALRM, +[](int sig) {
        static const char msg[] = "==> SIGALRM tripwire, exiting\n";
        ::write(STDERR_FILENO, msg, sizeof(msg) - 1);
        _exit(128 + sig);
    });

#   if defined(XR_PLATFORM_WINDOWS)
    std::signal(SIGABRT_COMPAT, +[](int sig) { handler_base(sig, "application is aborting"); });
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    _set_invalid_parameter_handler(&invalid_parameter_handler);
    _set_new_mode(1);
    _set_new_handler(&out_of_memory_handler);
    _set_purecall_handler(+[] { handler_base(SIGABRT, "pure virtual function call"); });
#   endif

#   ifdef USE_BUG_TRAP
    BT_SetTerminate();
#   else
    std::set_terminate(xr_terminate);
#   endif
#endif
}

void xrDebug::OnThreadExit()
{
#ifndef __SANITIZE_ADDRESS__
    std::signal(SIGINT,  nullptr);
    std::signal(SIGILL,  nullptr);
    std::signal(SIGFPE,  nullptr);
    std::signal(SIGSEGV, nullptr);
    std::signal(SIGABRT, nullptr);
    std::signal(SIGTERM, nullptr);
#   if defined(XR_PLATFORM_LINUX) || defined(XR_PLATFORM_APPLE) || defined(XR_PLATFORM_BSD)
    std::signal(SIGALRM, nullptr);
#   endif
    std::set_terminate(nullptr);

#   if defined(XR_PLATFORM_WINDOWS)
    std::signal(SIGABRT_COMPAT, nullptr);
    _set_abort_behavior(0, 0);
    _set_invalid_parameter_handler(nullptr);
    _set_new_mode(1);
    _set_new_handler(nullptr);
    _set_purecall_handler(nullptr);
#   endif
#endif
}

void xrDebug::Initialize(pcstr commandLine)
{
    ZoneScoped;
    *BugReportFile = 0;
    OnThreadSpawn();
    SetupExceptionHandler();
    SDL_SetAssertionHandler(SDLAssertionHandler, nullptr);
    // exception handler to all "unhandled" exceptions
#if defined(XR_PLATFORM_WINDOWS)
    PrevFilter = SetUnhandledExceptionFilter(UnhandledFilter);
#endif
#ifdef MASTER_GOLD
    ShowErrorMessage = commandLine ? !!strstr(commandLine, "-show_error_window") : false;
#endif
}

void xrDebug::Finalize()
{
    OnThreadExit();
    SDL_SetAssertionHandler(nullptr, nullptr);
#if defined(XR_PLATFORM_WINDOWS)
    SetUnhandledExceptionFilter(nullptr);
#endif
#ifdef MASTER_GOLD
    ShowErrorMessage = false;
#endif
}
