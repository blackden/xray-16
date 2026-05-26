#include "stdafx.h"
#include "GameSpy_HTTP.h"

#if defined(XR_PLATFORM_APPLE)
#include <atomic>
#include <functional>

// Worker queue API defined in ghttp_worker_apple.mm. extern "C" surface for
// the install / shutdown / drain-hook plumbing; C++-linkage surface for the
// std::function dispatch + completion enqueue (same translation unit family,
// same libc++, ABI-compatible). Forward-declared here rather than in a
// header because no other engine TU pokes the worker.
extern "C" void OpenXRay_GhttpInstallWorker(void);
extern "C" void OpenXRay_GhttpShutdownWorker(void);
extern "C" void OpenXRay_GhttpDrainCompletions(void);
extern "C" void OpenXRay_GhttpDiscardPendingCompletions(void);
// Defined in src/xrEngine/Engine.cpp under XR_PLATFORM_APPLE. In
// BUILD_SHARED_LIBS=ON each library is linked standalone, so the
// xrGameSpy CMakeLists.txt explicitly links xrEngine under APPLE to
// satisfy this symbol; the static-lib build resolves it through the
// xr_3da link order. See the link-deps comment in
// src/xrGameSpy/CMakeLists.txt for why the dep direction (downstream
// referencing upstream) is acceptable here.
extern "C" void OpenXRay_RegisterGhttpDrainHook(void (*hook)(void));
void OpenXRay_GhttpEnqueueCompletion(std::function<void()> invoke);
void OpenXRay_GhttpDispatchAsync(std::function<void()> work);
void OpenXRay_GhttpDispatchSync(std::function<void()> work);

// m_LastRequest after A.2 is worker-private — the only main-thread reader is
// StopDownload, which snapshots the value to schedule a cancel on the worker.
// The atomic is overkill for snapshot-on-main / write-on-worker (a relaxed
// load/store would do), but it documents the cross-thread channel for
// future readers. See notes/decisions/a2-ghttp-ctx-classification.md, the
// "not-quite-ctx hazard" finding on m_LastRequest.
static std::atomic<GHTTPRequest> g_lastRequestApple{static_cast<GHTTPRequest>(-1)};
#endif

CGameSpy_HTTP::CGameSpy_HTTP()
{
    m_LastRequest = -1;
    StartUp();
};

CGameSpy_HTTP::~CGameSpy_HTTP() { CleanUp(); }

void CGameSpy_HTTP::StartUp()
{
#if defined(XR_PLATFORM_APPLE)
    // Bring the dispatch worker up before the first ghttpStartup so the queue
    // exists by the time any Think / Get / Save call lands. Install is
    // idempotent — safe even if a second CGameSpy_HTTP is constructed (it
    // would not be, but the contract is preserved). After install, wire the
    // per-frame drain hook into the engine's macOS aggregator so completions
    // pushed by the worker actually reach the main thread.
    OpenXRay_GhttpInstallWorker();
    OpenXRay_RegisterGhttpDrainHook(&OpenXRay_GhttpDrainCompletions);
    OpenXRay_GhttpDispatchAsync([] { ghttpStartup(); });
#else
    ghttpStartup();
#endif
}

void CGameSpy_HTTP::CleanUp()
{
#if defined(XR_PLATFORM_APPLE)
    // Teardown order (audit finding, ctx-classification doc §3.3):
    //   1. Unregister the drain hook FIRST. Prevents the engine main loop from
    //      racing in a final drain while we're about to release the queue.
    //      The aggregator reads the hook pointer atomically; once nulled,
    //      OpenXRay_RunPerFrameMacOSHooks skips ghttp entirely.
    //   2. dispatch_sync(ghttpCleanup) — run ghttpCleanup as the FINAL block
    //      on the worker. This drains every pending Think / Save / Get task
    //      ahead of it (serial queue ordering) and tears down ghttp's
    //      C-global state on the same thread that's been touching it. Main
    //      blocks until cleanup returns.
    //   3. Discard any completion records the worker enqueued between step 1
    //      (hook unregister) and step 2 (ghttpCleanup return). The drain hook
    //      is gone — those records would never reach main, and they hold
    //      FastDelegate copies whose `this` pointer (e.g. CMainMenu*) is
    //      about to be destructed. Drop them without invoking, so no
    //      delegate fires into freed memory. Per cpp-engineer audit Bug 1.
    //   4. Shutdown the worker queue. dispatch_sync inside Shutdown is a
    //      second drain barrier (cheap — queue is already idle) plus the
    //      queue release.
    // Net effect: no in-flight worker block dereferences ghttp internals
    // after ghttpCleanup, no stranded completions hold dangling delegate
    // targets, and no main-thread drain races with worker teardown.
    OpenXRay_RegisterGhttpDrainHook(nullptr);
    OpenXRay_GhttpDispatchSync([] { ghttpCleanup(); });
    OpenXRay_GhttpDiscardPendingCompletions();
    OpenXRay_GhttpShutdownWorker();
#else
    ghttpCleanup();
#endif
}

void CGameSpy_HTTP::Think()
{
#if defined(XR_PLATFORM_APPLE)
    // ghttpThink is the per-frame polling tick that nc-stall reproducer
    // wedges on (gethostbyname / blocking select inside ghttp's state
    // machine). Marshal onto the worker; main returns immediately. The
    // serial queue keeps ghttp's single-threaded contract intact.
    OpenXRay_GhttpDispatchAsync([] { ghttpThink(); });
#else
    ghttpThink();
#endif
}

string128 GHTTPResultStr[] = {
    "GHTTPSuccess", // 0:  Successfully retrieved file.
    "GHTTPOutOfMemory", // 1:  A memory allocation failed.
    "GHTTPBufferOverflow", // 2:  The user-supplied buffer was too small to hold the file.
    "GHTTPParseURLFailed", // 3:  There was an error parsing the URL.
    "GHTTPHostLookupFailed", // 4:  Failed looking up the hostname.
    "GHTTPSocketFailed", // 5:  Failed to create/initialize/read/write a socket.
    "GHTTPConnectFailed", // 6:  Failed connecting to the http server.
    "GHTTPBadResponse", // 7:  Error understanding a response from the server.
    "GHTTPRequestRejected", // 8:  The request has been rejected by the server.
    "GHTTPUnauthorized", // 9:  Not authorized to get the file.
    "GHTTPForbidden", // 10: The server has refused to send the file.
    "GHTTPFileNotFound", // 11: Failed to find the file on the server.
    "GHTTPServerError", // 12: The server has encountered an internal error.
    "GHTTPFileWriteFailed", // 13: An error occured writing to the local file (for ghttpSaveFile[Ex]).
    "GHTTPFileReadFailed", // 14: There was an error reading from a local file (for posting files from disk).
    "GHTTPFileIncomplete", // 15: Download started but was interrupted.  Only reported if file size is known.
    "GHTTPFileToBig", // 16: The file is to big to be downloaded (size exceeds range of interal data types)
    "GHTTPEncryptionError", // 17: Error with encryption engine.
    "GHTTPRequestCancelled" // 18: User requested cancel and/or graceful close.
};

// Contexts are heap-allocated and freed inside the completion handler. Callers
// pass FastDelegates by reference but those reference engine-owned state that
// must outlive the request — typically members of a long-lived object.
class FileDownloadContext
{
public:
    using CompletionCallback = CGameSpy_HTTP::CompletionCallback;
    using ProgressCallback = CGameSpy_HTTP::ProgressCallback;

    CompletionCallback Completed;
    ProgressCallback Progress;

    FileDownloadContext(CompletionCallback& completed, ProgressCallback& progress)
        : Completed(completed), Progress(progress)
    {
    }
};

class StringFetchContext
{
public:
    using StringCompletionCallback = CGameSpy_HTTP::StringCompletionCallback;

    StringCompletionCallback Completed;

    explicit StringFetchContext(StringCompletionCallback& completed) : Completed(completed) {}
};

void __cdecl ProgressHandler(GHTTPRequest request, GHTTPState state, const char* buffer, GHTTPByteCount bufferLen,
    GHTTPByteCount received, GHTTPByteCount total, void* param)
{
    auto ctx = static_cast<FileDownloadContext*>(param);
    if (state == GHTTPReceivingFile && total)
    {
#if defined(XR_PLATFORM_APPLE)
        // Called on the worker thread. Enqueue the progress callback for the
        // main thread to invoke at the next frame boundary. Capture the
        // FastDelegate by value plus the two integer args; lambda runs on
        // main. Progress can fire many times per download — each fires a
        // separate drain record. Coalescing is a candidate optimisation but
        // not on the A.2 gate path.
        auto progress = ctx->Progress;
        OpenXRay_GhttpEnqueueCompletion([progress, received, total]() mutable {
            progress(received, total);
        });
#else
        ctx->Progress(received, total);
#endif
    }
}

GHTTPBool __cdecl CompletedHandler(
    GHTTPRequest request, GHTTPResult result, char* buffer, GHTTPByteCount bufferLen, void* param)
{
    auto ctx = static_cast<FileDownloadContext*>(param);
#if defined(XR_PLATFORM_APPLE)
    // Called on the worker thread. Build a completion record that the main
    // thread will invoke at frame boundary, then DELETE ctx — capture
    // FastDelegate by value, not pointer; the lambda owns its copy. ctx
    // itself is one-shot, deleted here on the worker (heap free is thread-
    // safe; xr_delete uses libc++ allocator under the hood).
    bool success = (result == GHTTPSuccess);
    if (!success)
        Msg("! CompletedCallBack Result - %s", GHTTPResultStr[result]);
    auto completed = ctx->Completed;
    OpenXRay_GhttpEnqueueCompletion([completed, success]() mutable {
        completed(success);
    });
    delete ctx;
#else
    switch (result)
    {
    case GHTTPSuccess: ctx->Completed(true); break;
    default:
        Msg("! CompletedCallBack Result - %s", GHTTPResultStr[result]);
        ctx->Completed(false);
        break;
    }
    delete ctx;
#endif
    return GHTTPTrue;
}

GHTTPBool __cdecl StringCompletedHandler(
    GHTTPRequest request, GHTTPResult result, char* buffer, GHTTPByteCount bufferLen, void* param)
{
    auto ctx = static_cast<StringFetchContext*>(param);
#if defined(XR_PLATFORM_APPLE)
    // Same pattern as CompletedHandler, but the success-path delegate gets a
    // buffer + length. ghttp's contract (header comment in GameSpy_HTTP.h):
    // body is only valid for the duration of the callback. We MUST deep-copy
    // before posting the record, since the lambda runs on main one frame
    // later — by then the worker has returned and ghttp may have freed the
    // buffer. xr_strdup-equivalent: explicit copy into an xr_string.
    bool success = (result == GHTTPSuccess);
    if (!success)
        Msg("! FetchString Result - %s", GHTTPResultStr[result]);
    auto completed = ctx->Completed;
    if (success && buffer)
    {
        std::string copy(buffer, static_cast<size_t>(bufferLen));
        u32 length = static_cast<u32>(bufferLen);
        OpenXRay_GhttpEnqueueCompletion([completed, copy = std::move(copy), length]() mutable {
            completed(true, copy.c_str(), length);
        });
    }
    else
    {
        OpenXRay_GhttpEnqueueCompletion([completed]() mutable {
            completed(false, nullptr, 0);
        });
    }
    delete ctx;
#else
    switch (result)
    {
    case GHTTPSuccess: ctx->Completed(true, buffer, static_cast<u32>(bufferLen)); break;
    default:
        Msg("! FetchString Result - %s", GHTTPResultStr[result]);
        ctx->Completed(false, nullptr, 0);
        break;
    }
    delete ctx;
#endif
    return GHTTPTrue;
}

void CGameSpy_HTTP::DownloadFile(LPCSTR URL, LPCSTR FileName, CompletionCallback& completed, ProgressCallback& progress)
{
    auto ctx = xr_new<FileDownloadContext>(completed, progress);
    Msg("URL:  %s", URL);
    Msg("File: %s", FileName);
#if defined(XR_PLATFORM_APPLE)
    // Marshal the ghttpSaveExA call onto the worker. URL and FileName are
    // C-strings owned by the caller (LPCSTR / const char*) — typically a
    // shared_str inside CMainMenu. xrGame's manifest fetch path keeps the
    // CMainMenu alive across the whole download, so the underlying string
    // storage outlives the worker block. To be safe against future callers,
    // copy both into the closure.
    std::string url(URL ? URL : "");
    std::string file(FileName ? FileName : "");
    OpenXRay_GhttpDispatchAsync([url = std::move(url), file = std::move(file), ctx]() mutable {
        GHTTPRequest req = ghttpSaveExA(url.c_str(), file.c_str(), "", NULL,
            GHTTPFalse, GHTTPFalse, ProgressHandler, CompletedHandler, ctx);
        g_lastRequestApple.store(req, std::memory_order_release);
        Msg("Code: %d", req);
        if (req < 0)
        {
            // Early-fail enqueue: completed(false) must run on main, not on
            // the worker (audit finding §3.2). The lambda below is the same
            // shape as the success-path completion handler builds, just
            // without bouncing through ghttp.
            auto completed = ctx->Completed;
            OpenXRay_GhttpEnqueueCompletion([completed]() mutable {
                completed(false);
            });
            delete ctx;
        }
    });
    // m_LastRequest is worker-private on Apple — the cancel path reads
    // g_lastRequestApple, not this member. Left untouched here; no external
    // reader exists (grep confirms only this file references it).
#else
    m_LastRequest =
        ghttpSaveExA(URL, FileName, "", NULL, GHTTPFalse, GHTTPFalse, ProgressHandler, CompletedHandler, ctx);
    Msg("Code: %d", m_LastRequest);
    if (m_LastRequest < 0)
    {
        completed(false);
        xr_delete(ctx);
    }
#endif
}

void CGameSpy_HTTP::FetchString(LPCSTR URL, StringCompletionCallback& completed)
{
    auto ctx = xr_new<StringFetchContext>(completed);
    Msg("HTTP GET: %s", URL);
#if defined(XR_PLATFORM_APPLE)
    std::string url(URL ? URL : "");
    OpenXRay_GhttpDispatchAsync([url = std::move(url), ctx]() mutable {
        GHTTPRequest req = ghttpGetA(url.c_str(), GHTTPFalse, StringCompletedHandler, ctx);
        g_lastRequestApple.store(req, std::memory_order_release);
        Msg("Code: %d", req);
        if (req < 0)
        {
            auto completed = ctx->Completed;
            OpenXRay_GhttpEnqueueCompletion([completed]() mutable {
                completed(false, nullptr, 0);
            });
            delete ctx;
        }
    });
#else
    m_LastRequest = ghttpGetA(URL, GHTTPFalse, StringCompletedHandler, ctx);
    Msg("Code: %d", m_LastRequest);
    if (m_LastRequest < 0)
    {
        completed(false, nullptr, 0);
        xr_delete(ctx);
    }
#endif
}

void CGameSpy_HTTP::StopDownload()
{
#if defined(XR_PLATFORM_APPLE)
    // ghttpCancelRequest mutates ghttp's internal state machine — on Apple
    // that state machine lives on the worker queue, so cancel must ride the
    // same queue. Snapshot the worker-side last-request id at enqueue time
    // and run the cancel under the worker's serialisation. Main returns
    // immediately; the cancel takes effect by the next Think tick.
    OpenXRay_GhttpDispatchAsync([] {
        GHTTPRequest req = g_lastRequestApple.exchange(-1, std::memory_order_acq_rel);
        if (req != -1)
            ghttpCancelRequest(req);
    });
    m_LastRequest = -1;
#else
    if (m_LastRequest != -1)
        ghttpCancelRequest(m_LastRequest);
    m_LastRequest = -1;
#endif
}
