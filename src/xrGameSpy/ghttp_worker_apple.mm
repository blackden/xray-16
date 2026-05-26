// Serial dispatch_queue worker for ghttp/gsCore polling (gitea #117, A.2).
//
// Built as part of the A.2 native-shell step. Pattern mirrors
// src/xrEngine/macos_cocoa_shim.mm: extern "C" install / arm / shutdown
// surface, idempotent install, atomic-style state, frame-boundary
// completion drain (companion plumbing lives in src/xrEngine/Engine.cpp
// alongside the A.1 lifecycle pipeline).
//
// Wiring (install + drain + enqueue from the ghttp call sites) lands in
// commits 2-3. This file ships build-only — none of the symbols below
// are referenced from engine code yet.
//
// Design rationale (full context: notes/decisions/a2-ghttp-ctx-classification.md
// and the A.2 plan):
//
//   - Single serial dispatch_queue carries the ghttp state machine.
//     ghttp's C-globals are not thread-safe; one worker queue preserves
//     single-threaded semantics across all entry points (ghttpThink,
//     ghttpGetA, ghttpSaveExA, ghttpCancelRequest).
//   - QoS user-initiated: polling is user-facing (manifest fetch, patch
//     download) but not interactive — matches NSURLSession's default.
//   - dispatch_queue_set_specific + dispatch_get_specific is the modern
//     "am I on the worker queue?" probe; dispatch_get_current_queue has
//     been deprecated since macOS 10.9 and is unsafe under queue
//     targeting.
//   - Idempotent install + explicit shutdown so the worker can be torn
//     down deterministically at engine exit. Install lifetime is bounded
//     by the StartUp→CleanUp pair on CGameSpy_HTTP: Shutdown clears the
//     install flag so a future re-StartUp (test harness, defensive re-init)
//     re-creates the queue cleanly. cpp-engineer audit (post-A.2 commits
//     1-3) verified ordering: drain hook unregistered first, ghttpCleanup
//     synchronous on worker, stranded completions discarded, then worker
//     shutdown — no UAF window.
//
// Deliberately does NOT include "stdafx.h" / xrCore headers — those drag
// in src/Common/PlatformApple.inl which typedefs BOOL as int32_t and
// collides with <objc/objc.h>'s `typedef bool BOOL` once <dispatch/dispatch.h>
// pulls Foundation in. Mirrors the same exclusion in macos_cocoa_shim.mm.

#include <dispatch/dispatch.h>

#include <atomic>
#include <deque>
#include <functional>
#include <mutex>
#include <utility>

namespace
{
// Sentinel for dispatch_queue_set_specific. The address is the key; the
// value is anything non-NULL (we use the address again). dispatch_get_specific
// returns the value if invoked on a queue (or a queue targeting one) that
// had the key set, NULL otherwise.
static const void* const kWorkerQueueKey = static_cast<const void*>(&kWorkerQueueKey);

dispatch_queue_t g_workerQueue = nullptr;
std::atomic<bool> g_workerInstalled{false};

// Completion record. Worker thread builds one of these for each ghttp
// callback it receives (success or error), pushes onto g_completionQueue
// under g_completionMutex, and the main thread drains the queue at frame
// boundary via OpenXRay_GhttpDrainCompletions.
//
// `kind` is diagnostic only — useful at the lldb prompt; the actual delivery
// is opaque, via `invoke`. Commit 3 constructs `invoke` as a lambda that
// captures the FastDelegate (by value) plus its args (deep-copied buffer,
// error code, etc.) so the main thread just calls invoke() and forgets.
//
// Type erasure via std::function is deliberate: FastDelegate lives behind
// xrCore headers, which we cannot include under Objective-C++ (BOOL clash
// with <objc/objc.h>, see the include-block note above). Commit 3 builds
// the lambda inside src/xrGameSpy/GameSpy_HTTP.cpp where FastDelegate is
// available as a plain C++ type.
enum class GhttpCompletionKind : unsigned
{
    Unknown = 0,
    FileDownload,        // ghttp success path for ghttpSaveExA-flow
    FileDownloadError,   // ghttp failure / early-fail for ghttpSaveExA-flow
    StringFetch,         // ghttp success path for ghttpGetA-flow
    StringFetchError,    // ghttp failure / early-fail for ghttpGetA-flow
};

struct GhttpCompletion
{
    GhttpCompletionKind kind = GhttpCompletionKind::Unknown;
    std::function<void()> invoke;
};

std::mutex g_completionMutex;
std::deque<GhttpCompletion> g_completionQueue;
} // namespace

extern "C" void OpenXRay_GhttpInstallWorker(void)
{
    // Idempotent: install at most once for the life of the process.
    bool expected = false;
    if (!g_workerInstalled.compare_exchange_strong(expected, true))
        return;

    dispatch_queue_attr_t attrs = dispatch_queue_attr_make_with_qos_class(
        DISPATCH_QUEUE_SERIAL, QOS_CLASS_USER_INITIATED, 0);
    g_workerQueue = dispatch_queue_create("tech.fedorov.openxray.ghttp.worker", attrs);

    // Set the sentinel so OpenXRay_GhttpAssertOnWorkerQueue can recognise
    // blocks running on this queue (including blocks on queues that target
    // this one, should we ever introduce them).
    dispatch_queue_set_specific(g_workerQueue, kWorkerQueueKey,
        const_cast<void*>(kWorkerQueueKey), nullptr);
}

extern "C" void OpenXRay_RegisterGhttpDrainHook(void (*hook)(void));

extern "C" void OpenXRay_GhttpShutdownWorker(void)
{
    // Defensive null-hook before anything else. CGameSpy_HTTP::CleanUp already
    // does this as step 1 of its teardown sequence, but if a future caller
    // invokes Shutdown without going through CleanUp (test harness, partial
    // init failure path), an in-flight main-thread drain would still race
    // against queue release. Idempotent with CleanUp's earlier call —
    // OpenXRay_RegisterGhttpDrainHook is a single atomic store. Per
    // cpp-engineer audit Bug 7.
    OpenXRay_RegisterGhttpDrainHook(nullptr);

    if (!g_workerInstalled.load(std::memory_order_acquire))
        return;

    // Drain any in-flight blocks by submitting a synchronous barrier.
    // dispatch_sync on a serial queue waits for everything already enqueued
    // to finish before our empty block runs, which is the join we want.
    if (g_workerQueue)
        dispatch_sync(g_workerQueue, ^{});

    // Under ARC dispatch objects are reference-counted automatically; nilling
    // our strong ref releases the queue. Clear the install flag so a future
    // StartUp can re-create the worker; install lifetime is bounded by the
    // StartUp→CleanUp pair, not process-wide. Per cpp-engineer audit Bug 3.
    g_workerQueue = nullptr;
    g_workerInstalled.store(false, std::memory_order_release);
}

// Discard any completion records the worker enqueued but the main thread has
// not yet drained. Called by CGameSpy_HTTP::CleanUp between ghttpCleanup and
// worker shutdown. After the drain hook is unregistered (CleanUp step 1) the
// main-thread per-frame drain stops firing, so any records the worker pushed
// just before ghttpCleanup ran would otherwise sit in g_completionQueue with
// dangling FastDelegate `this`-pointers (typically CMainMenu*) after the
// owning object's destructor runs. The std::function destructors free the
// captures cleanly without invoking them — no UAF, no surprise UI callback
// firing into freed memory. Per cpp-engineer audit Bug 1.
extern "C" void OpenXRay_GhttpDiscardPendingCompletions(void)
{
    std::deque<GhttpCompletion> drop;
    {
        std::lock_guard<std::mutex> lock(g_completionMutex);
        drop.swap(g_completionQueue);
    }
    // drop destructed here without invoke() — stranded FastDelegate-captured
    // `this` pointers must NOT be called after the owner is being torn down.
}

extern "C" bool OpenXRay_GhttpAssertOnWorkerQueue(void)
{
#ifdef DEBUG
    return dispatch_get_specific(kWorkerQueueKey) == kWorkerQueueKey;
#else
    return true;
#endif
}

// Drain pending ghttp completion records on the main thread. Registered as
// the per-frame ghttp drain hook (see OpenXRay_RegisterGhttpDrainHook in
// src/xrEngine/Engine.cpp); called once per frame from
// CRenderDevice::ProcessFrame after the A.1 lifecycle apply, so a
// system-sleep pause has a chance to suppress UI work that a completion
// might otherwise trigger.
//
// Swap-out under mutex, then drain without the mutex held — keeps the
// critical section O(1) regardless of completion count and lets enqueue
// from the worker proceed concurrently with delegate invocation on main.
//
// Commit 2 (this commit) ships the drain but no producers: enqueue calls
// land in commit 3 alongside the ghttp routing. Until then the queue is
// always empty and this drain is a no-op tick. The hook is also not
// registered yet, so even the no-op tick costs nothing until install
// runs in commit 3.
extern "C" void OpenXRay_GhttpDrainCompletions(void)
{
    std::deque<GhttpCompletion> local;
    {
        std::lock_guard<std::mutex> lock(g_completionMutex);
        if (g_completionQueue.empty())
            return;
        local.swap(g_completionQueue);
    }

    for (auto& record : local)
    {
        if (record.invoke)
            record.invoke();
    }
}

// Producer API for the worker thread to push completion records that the main
// thread will drain at the next frame boundary. C++ linkage (not extern "C")
// because we ship a std::function across the boundary — both sides of the
// xrGameSpy target compile under the same clang / libc++, so the ABI is
// guaranteed compatible inside one library. Call site lives in
// GameSpy_HTTP.cpp's static ghttp callbacks; the callbacks build the lambda
// where FastDelegate is a real type.
void OpenXRay_GhttpEnqueueCompletion(std::function<void()> invoke)
{
    if (!invoke)
        return;
    GhttpCompletion record;
    record.kind = GhttpCompletionKind::Unknown;
    record.invoke = std::move(invoke);
    std::lock_guard<std::mutex> lock(g_completionMutex);
    g_completionQueue.push_back(std::move(record));
}

// dispatch_async wrapper for the ghttp worker queue. C++ linkage for the same
// std::function-ABI reason as above. No-op if the worker is not installed —
// makes the helper safe to call before StartUp wires the worker up, and from
// the early-fail path of CleanUp after Shutdown nils the queue.
void OpenXRay_GhttpDispatchAsync(std::function<void()> work)
{
    if (!work)
        return;
    if (!g_workerInstalled.load(std::memory_order_acquire) || !g_workerQueue)
        return;
    // The block captures by copy; the C++ move into a heap-owned wrapper keeps
    // the closure alive past the block boundary. Apple's Blocks runtime treats
    // captured C++ objects with copy semantics — std::function is copyable, so
    // a direct capture works, but going through a unique_ptr keeps the move
    // semantics intact and avoids the implicit copy.
    auto* heldWork = new std::function<void()>(std::move(work));
    dispatch_async(g_workerQueue, ^{
        (*heldWork)();
        delete heldWork;
    });
}

// dispatch_sync wrapper for the ghttp worker queue. Used at teardown to drain
// the queue and run a final task (typically ghttpCleanup) as the last block
// on the worker before the queue is released. Safe to call from main only —
// dispatch_sync to a queue you're already on deadlocks; the worker queue is
// never the caller's queue (we never reenter from inside a worker block).
void OpenXRay_GhttpDispatchSync(std::function<void()> work)
{
    if (!work)
        return;
    if (!g_workerInstalled.load(std::memory_order_acquire) || !g_workerQueue)
    {
        // Worker not up — run inline so CleanUp ordering still completes
        // ghttpCleanup on the calling thread. Matches the "no-op when not
        // installed" semantics of the async variant for safety, but here the
        // caller wants the work to run regardless.
        work();
        return;
    }
    // dispatch_sync runs the block synchronously on the target queue and
    // returns when it finishes. No heap thunk needed — the block keeps the
    // C++ closure alive for its lifetime, which is bounded by this call.
    dispatch_sync(g_workerQueue, ^{
        work();
    });
}

