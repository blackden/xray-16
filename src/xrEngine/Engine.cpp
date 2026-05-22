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

#if defined(XR_PLATFORM_APPLE)
// C-linkage glue for the macOS Cocoa shim (macos_cocoa_shim.mm). That file
// cannot include Engine.h because xrCore headers conflict with Foundation
// types when compiled as Objective-C++ (see SKIP_PRECOMPILE_HEADERS in
// CMakeLists.txt). Same defer sequence as CCC_Quit::Execute in xr_ioc_cmd.cpp.
extern "C" void OpenXRay_RequestGracefulQuit()
{
    // Same fast-exit flag pattern as CCC_Quit and the SDL window-close handler.
    g_bShuttingDown = true;
    // XXX [POSTLOG_TEARDOWN_GAP]: diagnostic for gitea #52. Confirms the Cocoa
    // shim reaches the engine and defers both events. If this fires but
    // ==> eDisconnect dispatch does not, the issue is queue/dispatch timing.
    POSTLOG_MARK("OpenXRay_RequestGracefulQuit: Defer(disconnect, quit) issued");
    Engine.Event.Defer("KERNEL:disconnect");
    Engine.Event.Defer("KERNEL:quit");
}
#endif
