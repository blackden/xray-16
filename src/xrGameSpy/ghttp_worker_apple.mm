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
//     down deterministically at engine exit (cpp-engineer teardown
//     audit ahead of PR will verify ordering against ~CApplication).
//
// Deliberately does NOT include "stdafx.h" / xrCore headers — those drag
// in src/Common/PlatformApple.inl which typedefs BOOL as int32_t and
// collides with <objc/objc.h>'s `typedef bool BOOL` once <dispatch/dispatch.h>
// pulls Foundation in. Mirrors the same exclusion in macos_cocoa_shim.mm.

#include <dispatch/dispatch.h>

#include <atomic>

namespace
{
// Sentinel for dispatch_queue_set_specific. The address is the key; the
// value is anything non-NULL (we use the address again). dispatch_get_specific
// returns the value if invoked on a queue (or a queue targeting one) that
// had the key set, NULL otherwise.
static const void* const kWorkerQueueKey = static_cast<const void*>(&kWorkerQueueKey);

dispatch_queue_t g_workerQueue = nullptr;
std::atomic<bool> g_workerInstalled{false};
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

extern "C" void OpenXRay_GhttpShutdownWorker(void)
{
    if (!g_workerInstalled.load(std::memory_order_acquire))
        return;

    // Drain any in-flight blocks by submitting a synchronous barrier.
    // dispatch_sync on a serial queue waits for everything already enqueued
    // to finish before our empty block runs, which is the join we want.
    if (g_workerQueue)
        dispatch_sync(g_workerQueue, ^{});

    // Under ARC dispatch objects are reference-counted automatically; nilling
    // our strong ref releases the queue. We never re-install in the same
    // process (idempotent flag stays set) so this is a one-shot teardown.
    g_workerQueue = nullptr;
}

extern "C" bool OpenXRay_GhttpAssertOnWorkerQueue(void)
{
#ifdef DEBUG
    return dispatch_get_specific(kWorkerQueueKey) == kWorkerQueueKey;
#else
    return true;
#endif
}
