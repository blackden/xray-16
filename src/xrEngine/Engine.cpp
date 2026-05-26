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
#endif
