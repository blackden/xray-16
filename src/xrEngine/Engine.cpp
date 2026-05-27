// Engine.cpp: implementation of the CEngine class.
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "Engine.h"

#include "Common/PostLogMark.hpp"
#include "XR_IOConsole.h"
#include "xr_ioc_cmd.h"

struct SoundProcessor final : public pureFrame
{
    void OnFrame() override
    {
        GEnv.Sound->update(Device.vCameraPosition, Device.vCameraDirection, Device.vCameraTop, Device.vCameraRight);
    }
} g_sound_processor;

struct SoundRenderer final : public pureFrame
{
    void OnFrame() override
    {
        GEnv.Sound->render();
    }
} g_sound_renderer;

CEngine Engine;

CEngine::CEngine() {}
CEngine::~CEngine() {}

void CheckAndSetupRenderer()
{
    if (GEnv.isDedicatedServer)
    {
        Console->Execute("renderer renderer_r1");
        return;
    }

    if (strstr(Core.Params, "-rgl"))
        Console->Execute("renderer renderer_rgl");
    else if (strstr(Core.Params, "-r4"))
        Console->Execute("renderer renderer_r4");
    else if (strstr(Core.Params, "-r3"))
        Console->Execute("renderer renderer_r3");
    else if (strstr(Core.Params, "-r2.5"))
        Console->Execute("renderer renderer_r2.5");
    else if (strstr(Core.Params, "-r2a"))
        Console->Execute("renderer renderer_r2a");
    else if (strstr(Core.Params, "-r2"))
        Console->Execute("renderer renderer_r2");
    else if (strstr(Core.Params, "-r1"))
        Console->Execute("renderer renderer_r1");
    else
    {
        CCC_LoadCFG_custom cmd("renderer ");
        cmd.Execute(Console->ConfigFile);
        renderer_allow_override = true;
    }
}

extern void msCreate(pcstr name);

void CEngine::Initialize(GameModule* game, const std::array<RendererModule*, 2>& modules)
{
    ZoneScoped;
#ifdef DEBUG
    msCreate("game");
#endif

    eQuit = Event.Handler_Attach("KERNEL:quit", this);

    Device.seqFrame.Add(this, REG_PRIORITY_HIGH + 1000);

    Device.seqFrame.Add(&g_sound_processor, REG_PRIORITY_NORMAL - 1000); // Place it after Level update
    Device.seqFrameMT.Add(&g_sound_renderer);

    External.CreateRendererList(modules);
    CheckAndSetupRenderer();

    External.Initialize(game);
    Sheduler.Initialize();
}

void CEngine::Destroy()
{
    ZoneScoped;

    Sheduler.Destroy();
    External.Destroy();
    Event._destroy();

    Event.Handler_Detach(eQuit, this);

    Device.seqFrameMT.Remove(&g_sound_processor);
    Device.seqFrame.Remove(&g_sound_processor);
    Device.seqFrame.Remove(this);
}

void CEngine::OnEvent(EVENT E, u64 P1, u64 P2)
{
    if (E == eQuit)
    {
#if defined(XR_PLATFORM_APPLE)
        // Phase 1 diagnostic: did KERNEL:quit fire (engine initiating its own shutdown)?
        static const char marker[] = "==> KERNEL:quit handler fired (eQuit)\n";
        ::write(STDERR_FILENO, marker, sizeof(marker) - 1);
#endif
        if (pInput != nullptr)
            pInput->GrabInput(false);

        SDL_Event quit = { SDL_QUIT };
        SDL_PushEvent(&quit);
    }
}

void CEngine::OnFrame()
{
    Event.OnFrame();
}

void CEngine::RequestGracefulShutdown()
{
    // Active canary for unified-entry invariant (see #52, commit 7d2920852).
    // Every graceful quit path passes through here; OpenXRay_RequestGracefulQuit
    // marker alone catches only Cmd+Q via Cocoa, leaving 4 of 5 quit paths
    // (SDL CLOSE, console quit, menu Quit, future) unobservable without these.
    // If a future change re-fragments quit dispatch, the missing marker pair
    // in logs is the regression signal.
    POSTLOG_MARK("RequestGracefulShutdown: enter");
    g_bShuttingDown = true;
    Event.Defer("KERNEL:disconnect");
    Event.Defer("KERNEL:quit");
    POSTLOG_MARK("RequestGracefulShutdown: defers queued");
}

#if defined(XR_PLATFORM_APPLE)
// C-linkage glue for the macOS Cocoa shim (macos_cocoa_shim.mm). That file
// cannot include Engine.h because xrCore headers conflict with Foundation
// types when compiled as Objective-C++ (see SKIP_PRECOMPILE_HEADERS in
// CMakeLists.txt). Same defer sequence as CCC_Quit::Execute in xr_ioc_cmd.cpp.
extern "C" void OpenXRay_RequestGracefulQuit()
{
    // XXX [POSTLOG_TEARDOWN_GAP]: diagnostic for gitea #52. Confirms the Cocoa
    // shim reaches the engine and defers both events. If this fires but
    // ==> eDisconnect dispatch does not, the issue is queue/dispatch timing.
    POSTLOG_MARK("OpenXRay_RequestGracefulQuit: Defer(disconnect, quit) issued");
    Engine.RequestGracefulShutdown();
}

// Lifecycle event glue (gitea #114). NSWorkspace + NSApplication delegate
// callbacks land on the AppKit main thread; we cannot call Device.Pause() or
// Device.OnWindowActivate() synchronously from there because the render
// thread may be mid-frame (GL context bound, command buffer in flight). The
// setters below only flip an atomic enum; the render thread drains it at the
// start of CRenderDevice::ProcessFrame via OpenXRay_ApplyPendingLifecycleEvent.
namespace
{
enum class PendingLifecycleEvent : u32
{
    None = 0,
    SystemWillSleep,
    SystemDidWake,
    AppActivated,
    AppDeactivated,
};
std::atomic<PendingLifecycleEvent> g_pendingLifecycleEvent{PendingLifecycleEvent::None};

// Independent of the lifecycle enum because release-all may need to coexist
// with a same-frame AppDeactivated (Cmd-Tab fires both) or SystemWillSleep —
// folding it into the enum would let one overwrite the other on exchange().
std::atomic<bool> g_pendingReleaseAllKeys{false};
} // namespace

extern "C" void OpenXRay_OnSystemWillSleep(void)
{
    g_pendingLifecycleEvent.store(PendingLifecycleEvent::SystemWillSleep, std::memory_order_release);
}

extern "C" void OpenXRay_OnSystemDidWake(void)
{
    g_pendingLifecycleEvent.store(PendingLifecycleEvent::SystemDidWake, std::memory_order_release);
}

extern "C" void OpenXRay_OnAppDidBecomeActive(void)
{
    g_pendingLifecycleEvent.store(PendingLifecycleEvent::AppActivated, std::memory_order_release);
}

extern "C" void OpenXRay_OnAppWillResignActive(void)
{
    g_pendingLifecycleEvent.store(PendingLifecycleEvent::AppDeactivated, std::memory_order_release);
}

// Request deferred IR_ReleaseAll() at the next frame boundary (gitea #134).
// Replaces direct AppKit-thread invocation of OpenXRay_SyntheticReleaseAllKeys
// from focus-loss / sleep observers: pInput->IR_ReleaseAll() writes the
// keyboardState bitset AND fires IR_OnKeyboard{Release} into game receivers
// (UI / level / actor) that assume render-thread ownership of input — running
// the synchronous variant from the AppKit thread races KeyUpdate() reading
// the same bitset on the render thread. Setter is fire-and-forget: a second
// store before the first is drained is a no-op (idempotent).
extern "C" void OpenXRay_RequestReleaseAllKeys(void)
{
    g_pendingReleaseAllKeys.store(true, std::memory_order_release);
}

// Defined out-of-line in xr_input.cpp under XR_PLATFORM_APPLE. Both halves of
// the release-all sequence: pInput->IR_ReleaseAll() (drops every pressed
// scancode + mouse button) AND resets the NSEvent FlagsChanged baseline
// (g_lastShimModifierFlags = 0). The baseline reset is critical for the
// nsevent_input=1 pipeline: applicationDidBecomeActive: re-syncs the baseline
// to the current NSEvent.modifierFlags on return, but if we don't zero it
// here a stale value persists if focus return happens before any other
// pipeline event drains, producing a wrong XOR on the first post-return
// FlagsChanged.
extern "C" void OpenXRay_ApplyReleaseAllKeys(void);

void OpenXRay_ApplyPendingLifecycleEvent()
{
    const auto pending = g_pendingLifecycleEvent.exchange(PendingLifecycleEvent::None, std::memory_order_acq_rel);
    switch (pending)
    {
    case PendingLifecycleEvent::None:
        return;

    case PendingLifecycleEvent::SystemWillSleep:
        Msg("* OpenXRay: system will sleep — pausing");
        Device.Pause(TRUE, TRUE, TRUE, "system will sleep");
        break;

    case PendingLifecycleEvent::SystemDidWake:
        Msg("* OpenXRay: system did wake — resuming");
        // Device.Pause(FALSE, ...) already resets fTimeDelta to EPS_S+EPS_S
        // inside the unpause branch (Device.cpp:536), which absorbs the long
        // sleep-induced dt spike without an extra timer-reset call here.
        Device.Pause(FALSE, TRUE, TRUE, "system did wake");
        break;

    case PendingLifecycleEvent::AppActivated:
        // Idempotency: SDL's own focus dispatch (x_ray.cpp:452) may have
        // already activated us before this Cocoa-originated event drains.
        if (Device.m_sdlWnd && !Device.b_is_Active)
            Device.OnWindowActivate(Device.m_sdlWnd, true);
        break;

    case PendingLifecycleEvent::AppDeactivated:
        if (Device.m_sdlWnd && Device.b_is_Active)
            Device.OnWindowActivate(Device.m_sdlWnd, false);
        break;
    }
}

// Frame-boundary aggregator for macOS per-frame Cocoa hooks. Called from
// CRenderDevice::ProcessFrame (device.cpp). The fixed order encodes a real
// invariant:
//
//   1. Lifecycle apply runs first — if a system-sleep is pending it pauses
//      the device immediately, and the subsequent ghttp drain happens in a
//      paused-engine state (or the same frame's paused-state UI work).
//   2. Ghttp completion drain runs second — completion callbacks may push
//      UI events (manifest dialogs, patch download progress) that we would
//      not want to fire before lifecycle pause had a chance to suppress
//      them on a sleep-bound frame.
//
// Both halves no-op if their respective queues are empty, so the cost on
// the steady state is two atomic loads + one mutex try-lock.
//
// xrGameSpy lives downstream of xrEngine (xrGame depends on both, neither
// depends on the other), so we cannot reference ghttp_worker_apple.mm
// directly from here — that would break BUILD_SHARED_LIBS where each
// library is linked standalone. Instead xrGameSpy registers its drain
// function into the hook slot below at install time (commit 3 wiring);
// until then the slot is nullptr and the aggregator is just the lifecycle
// apply.
namespace
{
std::atomic<void (*)(void)> g_ghttpDrainHook{nullptr};
} // namespace

extern "C" void OpenXRay_RegisterGhttpDrainHook(void (*hook)(void))
{
    g_ghttpDrainHook.store(hook, std::memory_order_release);
}

// Drain the pending release-all flag (gitea #134). Runs BEFORE the lifecycle
// apply on purpose: AppDeactivated triggers seqAppDeactivate which calls
// CInput::OnAppDeactivate(), and that already zeroes keyboardState/mouseState
// without firing IR_OnKeyboard{Release} callbacks. If we ran release-all after
// the bitset reset, IR_ReleaseAll() would iterate over an all-zero bitset and
// emit nothing — game receivers (UI, level, actor) would never learn that the
// keys went up. Running release-all first lets it fire callbacks AND clear
// the bitset; the subsequent OnAppDeactivate reset becomes a no-op.
void OpenXRay_DrainPendingReleaseAllKeys()
{
    if (!g_pendingReleaseAllKeys.exchange(false, std::memory_order_acq_rel))
        return;
    OpenXRay_ApplyReleaseAllKeys();
}

void OpenXRay_RunPerFrameMacOSHooks()
{
    OpenXRay_DrainPendingReleaseAllKeys();
    OpenXRay_ApplyPendingLifecycleEvent();
    if (auto* hook = g_ghttpDrainHook.load(std::memory_order_acquire))
        hook();
}
#endif
