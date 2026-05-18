// xrCore.cpp : Defines the entry point for the DLL application.
//
#include "stdafx.h"
#pragma hdrstop

#if defined(XR_PLATFORM_WINDOWS)
#include <mmsystem.h>
#include <objbase.h>
#pragma comment(lib, "winmm.lib")
#elif defined(XR_PLATFORM_POSIX)
#include <sys/types.h>
#include <sys/stat.h>
#include <pwd.h>
#include <unistd.h>
#if defined(XR_PLATFORM_APPLE)
#include <iconv.h>
#endif
#endif
#include "xrCore.h"
#include "xrCore/_std_extensions.h"
#include "Threading/TaskManager.hpp"

#include <SDL.h>

#if __has_include(".GitInfo.hpp")
#include ".GitInfo.hpp"
#endif

#include "Compression/compression_ppmd_stream.h"
extern compression::ppmd::stream* trained_model;

XRCORE_API xrCore Core;

XRCORE_API int g_r__trace_encoding = 0;

XRCORE_API void xr_utf8_to_cp1251(char* buf, size_t buf_size)
{
#if defined(XR_PLATFORM_APPLE)
    if (!buf || buf_size == 0)
        return;
    iconv_t cd = iconv_open("CP1251//TRANSLIT", "UTF-8");
    if (cd == (iconv_t)-1)
        return;
    char tmp[1024] = {};
    char* in = buf;
    char* out = tmp;
    size_t in_left = strnlen(buf, buf_size);
    size_t out_left = sizeof(tmp) - 1;
    if (iconv(cd, &in, &in_left, &out, &out_left) != (size_t)-1)
    {
        *out = '\0';
        const size_t copy = std::min<size_t>(sizeof(tmp) - 1, buf_size - 1);
        memcpy(buf, tmp, copy);
        buf[copy] = '\0';
    }
    iconv_close(cd);
#else
    // Windows ANSI APIs already return cp1251, no conversion required.
    (void)buf;
    (void)buf_size;
#endif
}

XRCORE_API void xr_legacy_to_utf8(char* buf, size_t buf_size, pcstr codepage)
{
#if defined(XR_PLATFORM_APPLE)
    if (!buf || buf_size == 0 || !codepage)
        return;
    iconv_t cd = iconv_open("UTF-8", codepage);
    if (cd == (iconv_t)-1)
        return;
    char tmp[1024] = {};
    char* in = buf;
    char* out = tmp;
    size_t in_left = strnlen(buf, buf_size);
    size_t out_left = sizeof(tmp) - 1;
    if (iconv(cd, &in, &in_left, &out, &out_left) != (size_t)-1)
    {
        *out = '\0';
        const size_t copy = std::min<size_t>(sizeof(tmp) - 1, buf_size - 1);
        memcpy(buf, tmp, copy);
        buf[copy] = '\0';
    }
    iconv_close(cd);
#else
    (void)buf;
    (void)buf_size;
    (void)codepage;
#endif
}

XRCORE_API void xr_cp1251_to_utf8(char* buf, size_t buf_size)
{
    xr_legacy_to_utf8(buf, buf_size, "CP1251");
}

XRCORE_API bool xr_legacy_to_utf8_alloc(pcstr src, size_t src_len, pcstr codepage, xr_string& out)
{
    out.clear();
#if defined(XR_PLATFORM_APPLE)
    if (!src || !codepage)
        return false;
    iconv_t cd = iconv_open("UTF-8", codepage);
    if (cd == (iconv_t)-1)
        return false;
    // UTF-8 expansion factor over single-byte codepages caps at 3x (a single
    // byte never yields more than three UTF-8 bytes for cp1251/cp1250/cp1252
    // since all targets are inside the BMP). 4x leaves headroom.
    out.resize(src_len * 4 + 1);
    char* inp = const_cast<char*>(src);
    size_t in_left = src_len;
    char* outp = &out[0];
    size_t out_left = out.size() - 1;
    bool ok = (iconv(cd, &inp, &in_left, &outp, &out_left) != (size_t)-1);
    iconv_close(cd);
    if (!ok)
    {
        out.clear();
        return false;
    }
    *outp = '\0';
    out.resize(static_cast<size_t>(outp - &out[0]));
    return true;
#else
    (void)src;
    (void)src_len;
    (void)codepage;
    return false;
#endif
}

// Windows-1251 -> Unicode mapping (BMP only). 0x00..0x7F is ASCII identity;
// 0x80..0xFF is the cp1251 high half. 0x98 is unassigned in Windows-1251 -
// represented as U+FFFD so callers see "replacement" instead of a stray
// BMP codepoint. Source: Microsoft cp1251 reference + verified against
// iconv in tests/cp1251_codepoint_test.cpp.
extern XRCORE_API const u16 xr_cp1251_to_unicode[256] = {
    0x0000, 0x0001, 0x0002, 0x0003, 0x0004, 0x0005, 0x0006, 0x0007,
    0x0008, 0x0009, 0x000A, 0x000B, 0x000C, 0x000D, 0x000E, 0x000F,
    0x0010, 0x0011, 0x0012, 0x0013, 0x0014, 0x0015, 0x0016, 0x0017,
    0x0018, 0x0019, 0x001A, 0x001B, 0x001C, 0x001D, 0x001E, 0x001F,
    0x0020, 0x0021, 0x0022, 0x0023, 0x0024, 0x0025, 0x0026, 0x0027,
    0x0028, 0x0029, 0x002A, 0x002B, 0x002C, 0x002D, 0x002E, 0x002F,
    0x0030, 0x0031, 0x0032, 0x0033, 0x0034, 0x0035, 0x0036, 0x0037,
    0x0038, 0x0039, 0x003A, 0x003B, 0x003C, 0x003D, 0x003E, 0x003F,
    0x0040, 0x0041, 0x0042, 0x0043, 0x0044, 0x0045, 0x0046, 0x0047,
    0x0048, 0x0049, 0x004A, 0x004B, 0x004C, 0x004D, 0x004E, 0x004F,
    0x0050, 0x0051, 0x0052, 0x0053, 0x0054, 0x0055, 0x0056, 0x0057,
    0x0058, 0x0059, 0x005A, 0x005B, 0x005C, 0x005D, 0x005E, 0x005F,
    0x0060, 0x0061, 0x0062, 0x0063, 0x0064, 0x0065, 0x0066, 0x0067,
    0x0068, 0x0069, 0x006A, 0x006B, 0x006C, 0x006D, 0x006E, 0x006F,
    0x0070, 0x0071, 0x0072, 0x0073, 0x0074, 0x0075, 0x0076, 0x0077,
    0x0078, 0x0079, 0x007A, 0x007B, 0x007C, 0x007D, 0x007E, 0x007F,
    0x0402, 0x0403, 0x201A, 0x0453, 0x201E, 0x2026, 0x2020, 0x2021, // 0x80..0x87
    0x20AC, 0x2030, 0x0409, 0x2039, 0x040A, 0x040C, 0x040B, 0x040F, // 0x88..0x8F
    0x0452, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, // 0x90..0x97
    0xFFFD, 0x2122, 0x0459, 0x203A, 0x045A, 0x045C, 0x045B, 0x045F, // 0x98..0x9F  (0x98 unassigned)
    0x00A0, 0x040E, 0x045E, 0x0408, 0x00A4, 0x0490, 0x00A6, 0x00A7, // 0xA0..0xA7
    0x0401, 0x00A9, 0x0404, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x0407, // 0xA8..0xAF
    0x00B0, 0x00B1, 0x0406, 0x0456, 0x0491, 0x00B5, 0x00B6, 0x00B7, // 0xB0..0xB7
    0x0451, 0x2116, 0x0454, 0x00BB, 0x0458, 0x0405, 0x0455, 0x0457, // 0xB8..0xBF
    0x0410, 0x0411, 0x0412, 0x0413, 0x0414, 0x0415, 0x0416, 0x0417, // 0xC0..0xC7  А..З
    0x0418, 0x0419, 0x041A, 0x041B, 0x041C, 0x041D, 0x041E, 0x041F, // 0xC8..0xCF  И..П
    0x0420, 0x0421, 0x0422, 0x0423, 0x0424, 0x0425, 0x0426, 0x0427, // 0xD0..0xD7  Р..Ч
    0x0428, 0x0429, 0x042A, 0x042B, 0x042C, 0x042D, 0x042E, 0x042F, // 0xD8..0xDF  Ш..Я
    0x0430, 0x0431, 0x0432, 0x0433, 0x0434, 0x0435, 0x0436, 0x0437, // 0xE0..0xE7  а..з
    0x0438, 0x0439, 0x043A, 0x043B, 0x043C, 0x043D, 0x043E, 0x043F, // 0xE8..0xEF  и..п
    0x0440, 0x0441, 0x0442, 0x0443, 0x0444, 0x0445, 0x0446, 0x0447, // 0xF0..0xF7  р..ч
    0x0448, 0x0449, 0x044A, 0x044B, 0x044C, 0x044D, 0x044E, 0x044F, // 0xF8..0xFF  ш..я
};

XRCORE_API bool xr_is_valid_utf8(pcstr buf)
{
    if (!buf)
        return true;
    const unsigned char* p = reinterpret_cast<const unsigned char*>(buf);
    while (*p)
    {
        if (*p < 0x80)
        {
            ++p;
        }
        else if ((*p & 0xE0) == 0xC0)
        {
            if ((p[1] & 0xC0) != 0x80) return false;
            // overlong: must be >= 0xC2
            if (*p < 0xC2) return false;
            p += 2;
        }
        else if ((*p & 0xF0) == 0xE0)
        {
            if ((p[1] & 0xC0) != 0x80 || (p[2] & 0xC0) != 0x80) return false;
            // overlong / surrogate: U+0800..U+FFFF excluding D800..DFFF
            if (*p == 0xE0 && p[1] < 0xA0) return false;
            if (*p == 0xED && p[1] >= 0xA0) return false;
            p += 3;
        }
        else if ((*p & 0xF8) == 0xF0)
        {
            if ((p[1] & 0xC0) != 0x80 || (p[2] & 0xC0) != 0x80 || (p[3] & 0xC0) != 0x80) return false;
            if (*p == 0xF0 && p[1] < 0x90) return false; // overlong
            if (*p == 0xF4 && p[1] >= 0x90) return false; // > U+10FFFF
            if (*p > 0xF4) return false;
            p += 4;
        }
        else
        {
            return false;
        }
    }
    return true;
}

static u32 init_counter = 0;

#define DO_EXPAND(...) __VA_ARGS__##1
#define EXPAND(VAL) DO_EXPAND(VAL)

#ifdef CI
#if EXPAND(CI) == 1
#undef CI
#endif
#endif

#ifdef APPVEYOR
#if EXPAND(APPVEYOR) == 1
#undef APPVEYOR
#endif
#endif

#ifdef GITHUB_ACTIONS
#if EXPAND(GITHUB_ACTIONS) == 1
#undef GITHUB_ACTIONS
#endif
#endif

#ifndef GIT_INFO_CURRENT_COMMIT
#define GIT_INFO_CURRENT_COMMIT "unknown"
#endif

#ifndef GIT_INFO_CURRENT_BRANCH
#define GIT_INFO_CURRENT_BRANCH "unknown"
#endif

void SDLLogOutput(void* userdata, int category, SDL_LogPriority priority, const char* message);

const pcstr xrCore::buildDate = __DATE__;
const pcstr xrCore::buildCommit = GIT_INFO_CURRENT_COMMIT;
const pcstr xrCore::buildBranch = GIT_INFO_CURRENT_BRANCH;

void SanitizeString(pcstr str)
{
    pstr mut_str = const_cast<pstr>(str);

    while (*mut_str != '\0')
    {
        switch (*mut_str)
        {
        case '\\':
        case '/':
        case ',':
        case '.':
            *mut_str = '_';
            [[fallthrough]];
        default:
            ++mut_str;
        }
    }
}

xrCore::xrCore()
    : ApplicationName{}, ApplicationPath{},
      WorkingPath{},
      UserName{}, CompName{},
      Params(nullptr), dwFrame(0),
      PluginMode(false)
{
    CalculateBuildId();
}

void xrCore::CalculateBuildId()
{
    const int startDay = 31;
    const int startMonth = 1;
    const int startYear = 1999;
    const char* monthId[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    const int daysInMonth[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    int days;
    int months = 0;
    int years;
    string16 month;
    string256 buffer;
    xr_strcpy(buffer, buildDate);
    sscanf(buffer, "%s %d %d", month, &days, &years);
    for (int i = 0; i < 12; i++)
    {
        if (xr_stricmp(monthId[i], month))
            continue;
        months = i;
        break;
    }
    buildId = (years - startYear) * 365 + days - startDay;
    for (int i = 0; i < months; i++)
        buildId += daysInMonth[i];
    for (int i = 0; i < startMonth - 1; i++)
        buildId -= daysInMonth[i];
}

void xrCore::PrintBuildInfo()
{
    Msg("%s %s build %d, %s (%s)", ApplicationName, XRAY_BUILD_CONFIGURATION, buildId, buildDate, XRAY_BUILD_CONFIGURATION2);

    pcstr name      = "Custom";
    pcstr buildUniqueId = nullptr;
    pcstr buildId   = nullptr;
    pcstr builder   = nullptr;
    pcstr commit    = GetBuildCommit();
    pcstr branch    = GetBuildBranch();

#if defined(CI)
#   if defined(APPVEYOR)
    name            = "AppVeyor";
    buildUniqueId   = APPVEYOR_BUILD_ID;
    buildId         = APPVEYOR_BUILD_VERSION;
    builder         = APPVEYOR_ACCOUNT_NAME;
#   elif defined(GITHUB_ACTIONS)
    name            = "GitHub Actions";
    buildUniqueId   = GITHUB_RUN_ID;
    buildId         = GITHUB_RUN_NUMBER;
    builder         = GITHUB_REPOSITORY;
#else
#   pragma TODO("PrintBuildInfo for other CIs")
    name            = "CI";
    builder         = "Unknown CI";
#   endif
#endif

    string512 buf;
    strconcat(buf, name, " build "); // "%s build "

    if (buildId)
    {
        strconcat(buf, buf, buildId, " "); // "id "
        if (buildUniqueId)
            strconcat(buf, buf, "(", buildUniqueId, ") "); // "(unique id) "
    }

    strconcat(buf, buf, "from commit[", commit, "]"); // "from commit[hash]"
    strconcat(buf, buf, " branch[", branch, "]"); // " branch[name]"

    if (builder)
        strconcat(buf, buf, " (built by ", builder, ")"); // " (built by builder)"

    Log(buf); // "%s build %s from commit[%s] branch[%s] (built by %s)"
}

void xrCore::Initialize(pcstr _ApplicationName, pcstr commandLine, bool init_fs, pcstr fs_fname, bool plugin)
{
    ZoneScoped;
    xr_strcpy(ApplicationName, _ApplicationName);
    PrintBuildInfo();

    if (0 == init_counter)
    {
#if defined(XR_ARCHITECTURE_X86) || defined(XR_ARCHITECTURE_X64)
        R_ASSERT2(CPU::HasSSE2, "Your CPU must support SSE2.");
#endif

        PluginMode = plugin;
        if (commandLine)
            Params = xr_strdup(commandLine);
        else
            Params = xr_strdup("");

#if defined(XR_PLATFORM_WINDOWS)
        timeBeginPeriod(1);
        CoInitializeEx(nullptr, COINIT_MULTITHREADED); // needed for OpenAL initialization

        string_path fn, dr, di;

        // application path
        GetModuleFileName(GetModuleHandle("xrCore"), fn, sizeof(fn));
        _splitpath(fn, dr, di, nullptr, nullptr);
        strconcat(sizeof(ApplicationPath), ApplicationPath, dr, di);
#elif defined(XR_PLATFORM_POSIX)
        char* base_path = SDL_GetBasePath();
        if (!base_path)
        {
            if (strstr(Core.Params, "-shoc") || strstr(Core.Params, "-soc"))
                base_path = SDL_GetPrefPath("GSC Game World", "S.T.A.L.K.E.R. - Shadow of Chernobyl");
            else if (strstr(Core.Params, "-cs"))
                base_path = SDL_GetPrefPath("GSC Game World", "S.T.A.L.K.E.R. - Clear Sky");
            else
                base_path = SDL_GetPrefPath("GSC Game World", "S.T.A.L.K.E.R. - Call of Pripyat");
        }
        SDL_strlcpy(ApplicationPath, base_path, sizeof(ApplicationPath));
        SDL_free(base_path);
#else
#   error Select or add implementation for your platform
#endif

#ifdef _EDITOR
        // working path
        if (strstr(Params, "-wf"))
        {
            string_path c_name;
            sscanf(strstr(Core.Params, "-wf ") + 4, "%[^ ] ", c_name);
            SetCurrentDirectory(c_name);
        }
#endif

#if defined(XR_PLATFORM_WINDOWS)
        GetCurrentDirectory(sizeof(WorkingPath), WorkingPath);
#elif defined(XR_PLATFORM_POSIX)
        getcwd(WorkingPath, sizeof(WorkingPath));
#else
#   error Select or add implementation for your platform
#endif

#if defined(XR_PLATFORM_WINDOWS)
        // User/Comp Name
        DWORD sz_user = sizeof(UserName);
        GetUserName(UserName, &sz_user);

        DWORD sz_comp = sizeof(CompName);
        GetComputerName(CompName, &sz_comp);
#elif defined(XR_PLATFORM_POSIX)
        uid_t uid = geteuid();
        struct passwd *pw = getpwuid(uid);
        if (pw)
        {
            // Prefer pw_gecos (the full real name, "Илья Иванов" on a RU
            // account) so save filename prefixes and log headers carry the
            // human-readable identity. Falls back to pw_name (the ASCII
            // login) if gecos is empty.
            //
            // Pre-utf8 migration this was inverted: pw_name was preferred
            // because the cp1251 renderer turned UTF-8 cyrillic into
            // mojibake and APFS rejected the resulting cp1251 path bytes
            // with EILSEQ. After Phase 1 the renderer is codepoint-aware
            // and Phase 3.1 handles APFS retries, so pw_gecos is safe again.
            strncpy(UserName, pw->pw_gecos, sizeof(UserName) - 1);
            // pw_gecos can legitimately be empty (server accounts, some
            // distros); also some systems append ",,," fields to it -- trim.
            if (char* comma = strchr(UserName, ','))
                *comma = '\0';
            if (UserName[0] == '\0')
                strncpy(UserName, pw->pw_name, sizeof(UserName) - 1);
        }
        else
            Msg("! Failed to get user name");

        if (gethostname(CompName, sizeof(CompName)) == 0)
            CompName[sizeof(CompName) - 1] = '\0';
        else
            Msg("! Failed to get computer name");
#else
#   error Select or add implementation for your platform
#endif

        SanitizeString(UserName);
        SanitizeString(CompName);

#ifdef DEBUG
        Msg("UserName: %s", UserName);
        Msg("ComputerName: %s", CompName);
#endif

        Memory._initialize();

        SDL_LogSetOutputFunction(SDLLogOutput, nullptr);
        Msg("\ncommand line %s\n", Params);
        _initialize_cpu();
        TaskScheduler = xr_make_unique<TaskManager>();
        TaskScheduler->SpawnThreads();
        // xrDebug::Initialize ();

        rtc_initialize();

        xr_FS = xr_make_unique<CLocatorAPI>();

        xr_EFS = xr_make_unique<EFS_Utils>();
        //. R_ASSERT (co_res==S_OK);
    }
    if (init_fs)
    {
        u32 flags = 0u;
        if (strstr(Params, "-build") != nullptr)
            flags |= CLocatorAPI::flBuildCopy;
        if (strstr(Params, "-ebuild") != nullptr)
            flags |= CLocatorAPI::flBuildCopy | CLocatorAPI::flEBuildCopy;
#ifdef DEBUG
        if (strstr(Params, "-cache"))
            flags |= CLocatorAPI::flCacheFiles;
        else
            flags &= ~CLocatorAPI::flCacheFiles;
#endif // DEBUG
#ifdef _EDITOR // for EDITORS - no cache
        flags &= ~CLocatorAPI::flCacheFiles;
#endif // _EDITOR

// TODO Add proper check for CMake Windows build
#if !defined(XR_PLATFORM_WINDOWS)
        if (xr_stricmp(ApplicationPath, CMAKE_INSTALL_FULL_DATAROOTDIR) != 0)
            flags |= CLocatorAPI::flScanAppRoot;
#endif

#ifndef _EDITOR
#ifndef ELocatorAPIH
        if (strstr(Params, "-file_activity") != nullptr)
            flags |= CLocatorAPI::flDumpFileActivity;
#endif
#endif
        FS._initialize(flags, nullptr, fs_fname);
        EFS._initialize();
    }
    init_counter++;
}

void xrCore::_destroy()
{
    --init_counter;
    if (0 == init_counter)
    {
        ZoneScoped;
        FS._destroy();
        EFS._destroy();
        xr_FS = nullptr;
        xr_EFS = nullptr;

        if (trained_model)
        {
            void* buffer = trained_model->buffer();
            xr_free(buffer);
            xr_delete(trained_model);
        }
        TaskScheduler = nullptr;
        xr_free(Params);
        Memory._destroy();
#ifdef XR_PLATFORM_WINDOWS
        CoUninitialize();
        timeEndPeriod(1);
#endif
    }
}

void SDLLogOutput(void* /*userdata*/, int category, SDL_LogPriority priority, const char* message)
{
    pcstr from;
    switch (category)
    {
    case SDL_LOG_CATEGORY_APPLICATION:  from = "application"; break;
    case SDL_LOG_CATEGORY_ERROR:        from = "error"; break;
    case SDL_LOG_CATEGORY_ASSERT:       from = "assert"; break;
    case SDL_LOG_CATEGORY_SYSTEM:       from = "system"; break;
    case SDL_LOG_CATEGORY_AUDIO:        from = "audio"; break;
    case SDL_LOG_CATEGORY_VIDEO:        from = "video"; break;
    case SDL_LOG_CATEGORY_RENDER:       from = "render"; break;
    case SDL_LOG_CATEGORY_INPUT:        from = "input"; break;
    case SDL_LOG_CATEGORY_TEST:         from = "test"; break;
    case SDL_LOG_CATEGORY_CUSTOM:       from = "custom"; break;
    default:                            from = "unknown"; break;
    }

    char mark;
    pcstr type;
    switch (priority)
    {
    case SDL_LOG_PRIORITY_VERBOSE:      mark = '%'; type = "verbose"; break;
    case SDL_LOG_PRIORITY_DEBUG:        mark = '#'; type = "debug"; break;
    case SDL_LOG_PRIORITY_INFO:         mark = '='; type = "info"; break;
    case SDL_LOG_PRIORITY_WARN:         mark = '~'; type = "warn"; break;
    case SDL_LOG_PRIORITY_ERROR:        mark = '!'; type = "error"; break;
    case SDL_LOG_PRIORITY_CRITICAL:     mark = '$'; type = "critical"; break;
    default:                            mark = ' '; type = "unknown"; break;
    }

    static constexpr pcstr format = "%c [sdl][%s][%s]: %s";
    const size_t size = sizeof(mark) + sizeof(from) + sizeof(type) + sizeof(format) + sizeof(message);
    pstr buf = (pstr)xr_alloca(size);

    xr_sprintf(buf, size, format, mark, from, type, message);
    Log(buf);
}
