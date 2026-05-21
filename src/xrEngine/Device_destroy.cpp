#include "stdafx.h"

#include "Render.h"
#include "xr_input.h"

void CRenderDevice::Destroy()
{
    if (!b_is_Ready)
        return;

    ZoneScoped;
    Log("Destroying Render...");
    b_is_Ready = false;
    Statistic->OnDeviceDestroy();
    // Drain pending GPU work before tearing the renderer down. On Apple's
    // Metal-backed GL the glDelete* cascade in OnDeviceDestroy serializes
    // against in-flight shadow cascades / occlusion queries / streaming
    // uploads via mach_msg, which manifests as a multi-second TX-state
    // hang on Cmd+Q from inside a level. On other platforms FlushGpuQueue
    // is a no-op. See notes/bug-patterns.md family 6.
    GEnv.Render->FlushGpuQueue();
    GEnv.Render->OnDeviceDestroy(false);
    Memory.mem_compact();
    GEnv.Render->Destroy();
    seqRender.Clear();
    seqAppActivate.Clear();
    seqAppDeactivate.Clear();
    seqAppEnd.Clear();
    seqFrame.Clear();
    seqFrameMT.Clear();
    seqDeviceReset.Clear();
    seqParallel.clear();
    xr_delete(Statistic);

    SDL_DestroyWindow(m_sdlWnd);
    Log("Render destroyed.");
}

void CRenderDevice::Reset(bool precache /*= true*/)
{
    ZoneScoped;

    const auto dwWidth_before = dwWidth;
    const auto dwHeight_before = dwHeight;
    pInput->GrabInput(false);

    const auto tm_start = TimerAsync();

    m_imgui_render->OnDeviceResetBegin();

    UpdateWindowProps();
    GEnv.Render->Reset(m_sdlWnd, dwWidth, dwHeight, fWidth_2, fHeight_2);

    m_imgui_render->OnDeviceResetEnd();

    UpdateWindowProps(); // hack

    SetupStates();

    if (precache)
        PreCache(20, false);

    const auto tm_end = TimerAsync();
    int drawableW = 0, drawableH = 0;
    SDL_GL_GetDrawableSize(m_sdlWnd, &drawableW, &drawableH);
    Msg("*** RESET [%d ms] mode=%ux%u points=%dx%d drawable=%dx%d engine=%ux%u",
        tm_end - tm_start,
        psDeviceMode.Width, psDeviceMode.Height,
        m_rcWindowClient.w, m_rcWindowClient.h,
        drawableW, drawableH,
        dwWidth, dwHeight);

    // TODO: Remove this! It may hide crash
    Memory.mem_compact();

    seqDeviceReset.Process();
    if (dwWidth_before != dwWidth || dwHeight_before != dwHeight)
        seqUIReset.Process();

    if (!GEnv.isDedicatedServer)
        pInput->GrabInput(true);
}
