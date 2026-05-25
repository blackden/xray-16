#include "stdafx.h"

#include "xrCore/xr_token.h"
#include "xr_input.h"

xr_vector<xr_token> vid_monitor_token;
xr_map<u32, xr_vector<xr_token>> vid_mode_token;

void FillResolutionsForMonitor(const int monitorID)
{
    const int modeCount = SDL_GetNumDisplayModes(monitorID);
    R_ASSERT3(modeCount > 0, "Failed to find display modes", SDL_GetError());

    for (int i = modeCount - 1; i >= 0; --i)
    {
        SDL_DisplayMode mode;
        const int result = SDL_GetDisplayMode(monitorID, i, &mode);
        R_ASSERT3(result == 0, "Failed to find specified display mode", SDL_GetError());

        string256 buf;
        xr_sprintf(buf, sizeof(buf), "%ux%u (%dHz)", mode.w, mode.h, mode.refresh_rate);
        vid_mode_token[monitorID].emplace_back(xr_strdup(buf), i);
    }

    vid_mode_token[monitorID].emplace_back(nullptr, -1);
}

void FillImGuiMonitorData(const int monitorID)
{
    ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();

    // Warning: the validity of monitor DPI information on Windows
    // depends on the application DPI awareness settings,
    // which generally needs to be set in the manifest or at runtime.
    ImGuiPlatformMonitor monitor;
    SDL_Rect r;
    SDL_GetDisplayBounds(monitorID, &r);
    monitor.MainPos = monitor.WorkPos = ImVec2((float)r.x, (float)r.y);
    monitor.MainSize = monitor.WorkSize = ImVec2((float)r.w, (float)r.h);

    SDL_GetDisplayUsableBounds(monitorID, &r);
    monitor.WorkPos = ImVec2((float)r.x, (float)r.y);
    monitor.WorkSize = ImVec2((float)r.w, (float)r.h);

    // FIXME-VIEWPORT: On MacOS SDL reports actual monitor DPI scale, ignoring OS configuration. We may want to set
    //  DpiScale to cocoa_window.backingScaleFactor here.
    float dpi = 0.0f;
    if (!SDL_GetDisplayDPI(monitorID, &dpi, nullptr, nullptr))
        monitor.DpiScale = dpi / 96.0f;

    monitor.PlatformHandle = (void*)(intptr_t)monitorID;
    platform_io.Monitors.push_back(monitor);
}

void CRenderDevice::FillVideoModes()
{
    ZoneScoped;

    const int displayCount = SDL_GetNumVideoDisplays();
    R_ASSERT3(displayCount > 0, "Failed to find display", SDL_GetError());

    for (int i = 0; i < displayCount; ++i)
    {
        string256 buf;
        xr_sprintf(buf, "%d. %s", i, SDL_GetDisplayName(i));
        vid_monitor_token.emplace_back(xr_strdup(buf), i);

        FillResolutionsForMonitor(i);
        FillImGuiMonitorData(i);
    }
    vid_monitor_token.emplace_back(nullptr, -1);
}

void CRenderDevice::CleanupVideoModes()
{
    ZoneScoped;

    for (auto& [monitor_id, tokens] : vid_mode_token)
    {
        for (auto& token : tokens)
        {
            auto tokenName = const_cast<pstr>(token.name);
            xr_free(tokenName);
        }
        tokens.clear();
    }
    vid_mode_token.clear();

    for (auto& token : vid_monitor_token)
    {
        pstr tokenName = const_cast<pstr>(token.name);
        xr_free(tokenName);
    }
    vid_monitor_token.clear();

    ImGui::GetPlatformIO().Monitors.resize(0);
}

void CRenderDevice::SetWindowDraggable(bool draggable)
{
    // Only draggable if resizable too
    const bool windowed = psDeviceMode.WindowStyle == rsWindowed || psDeviceMode.WindowStyle == rsWindowedBorderless;
    const bool resizable = SDL_GetWindowFlags(Device.m_sdlWnd) & SDL_WINDOW_RESIZABLE;
    m_allowWindowDrag = draggable && windowed && resizable;

    SDL_SetWindowOpacity(Device.m_sdlWnd, m_allowWindowDrag ? 0.95f : 1.0f);
}

void CRenderDevice::UpdateWindowProps()
{
    ZoneScoped;

    const bool windowed = psDeviceMode.WindowStyle != rsFullscreen;
    SelectResolution(windowed);

    // Changing monitor, unset fullscreen for the previous monitor
    // and move the window to the new monitor
    if (SDL_GetWindowDisplayIndex(m_sdlWnd) != static_cast<int>(psDeviceMode.Monitor))
    {
        SDL_SetWindowFullscreen(m_sdlWnd, SDL_DISABLE);

        SDL_Rect rect;
        SDL_GetDisplayBounds(psDeviceMode.Monitor, &rect);
        SDL_SetWindowPosition(m_sdlWnd, rect.x, rect.y);
    }

    if (psDeviceMode.WindowStyle != rsFullscreenBorderless)
        SDL_SetWindowSize(m_sdlWnd, psDeviceMode.Width, psDeviceMode.Height);
    else
    {
        SDL_DisplayMode current;
        SDL_GetCurrentDisplayMode(psDeviceMode.Monitor, &current);

        SDL_SetWindowSize(m_sdlWnd, current.w, current.h);
    }

    if (windowed)
    {
        const bool drawBorders = psDeviceMode.WindowStyle == rsWindowed;
        const bool useDesktopFullscreen = b_is_Ready && psDeviceMode.WindowStyle == rsFullscreenBorderless;

        SDL_SetWindowBordered(m_sdlWnd, drawBorders ? SDL_TRUE : SDL_FALSE);
#if defined(XR_PLATFORM_APPLE)
        // macOS: НЕ снимаем RESIZABLE перед DESKTOP-fullscreen. SDL2 cocoa backend
        // (SDL_cocoawindow.m:2454-2476) при resizable=FALSE делает
        // setCollectionBehavior:NSWindowCollectionBehaviorFullScreenNone — окно
        // теряет eligibility на native Cocoa fullscreen Space, toggleFullScreen
        // falls back на borderless overlay, Cmd-Tab minimize'ит в Dock вместо
        // Space-switch. Native fullscreen Space сам блокирует resize. См. #99.
        SDL_SetWindowResizable(m_sdlWnd, SDL_TRUE);
#else
        SDL_SetWindowResizable(m_sdlWnd, !useDesktopFullscreen ? SDL_TRUE : SDL_FALSE);
#endif
        SDL_SetWindowFullscreen(m_sdlWnd, useDesktopFullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : SDL_DISABLE);
    }
    else if (b_is_Ready)
    {
#if defined(XR_PLATFORM_APPLE)
        // macOS: см. комментарий выше — RESIZABLE-bit оставляем, чтобы Cocoa
        // не downgrade'ил collectionBehavior до FullScreenNone и Cmd-Tab
        // продолжал переключать Spaces. См. #99.
#else
        SDL_SetWindowResizable(m_sdlWnd, SDL_FALSE);
#endif
#if defined(XR_PLATFORM_APPLE)
        // macOS: exclusive SDL_WINDOW_FULLSCREEN захватывает display эксклюзивно —
        // WindowServer блокирует Cmd-Tab (#50), а video-mode switching вызывает
        // geometry race в SDL_GL_GetDrawableSize ниже (#48). DESKTOP-режим даёт
        // borderless-окно во весь экран без mode-switch — race не возникает.
        SDL_DisplayMode current;
        SDL_GetCurrentDisplayMode(psDeviceMode.Monitor, &current);
        SDL_SetWindowSize(m_sdlWnd, current.w, current.h);
        SDL_SetWindowFullscreen(m_sdlWnd, SDL_WINDOW_FULLSCREEN_DESKTOP);
#else
        SDL_SetWindowFullscreen(m_sdlWnd, SDL_WINDOW_FULLSCREEN);

        SDL_DisplayMode mode;
        SDL_GetWindowDisplayMode(m_sdlWnd, &mode);
        mode.w = psDeviceMode.Width;
        mode.h = psDeviceMode.Height;
        mode.refresh_rate = psDeviceMode.RefreshRate;
        SDL_SetWindowDisplayMode(m_sdlWnd, &mode);
#endif
    }

    SDL_PumpEvents();
    UpdateWindowRects();

    // Replace point-sized dwWidth/dwHeight (set in SelectResolution from
    // psDeviceMode) with physical pixel drawable size. On HiDPI/Retina with
    // SDL_WINDOW_ALLOW_HIGHDPI this is 2x the points; on non-HiDPI it equals
    // the window size, so this is a no-op cross-platform. Render path (RT
    // sizes, glViewport, Present blit) consumes dwWidth/dwHeight as pixels.
    {
        int pxW = 0, pxH = 0;
        SDL_GL_GetDrawableSize(m_sdlWnd, &pxW, &pxH);
        if (pxW > 0 && pxH > 0)
        {
            dwWidth = static_cast<u32>(pxW);
            dwHeight = static_cast<u32>(pxH);
        }
    }

    ImGuiIO& io = ImGui::GetIO();

    io.DisplaySize = { static_cast<float>(psDeviceMode.Width), static_cast<float>(psDeviceMode.Height) };
    io.DisplayFramebufferScale = ImVec2{ float(dwWidth / m_rcWindowClient.w), float(dwHeight / m_rcWindowClient.h) };
}

void CRenderDevice::UpdateWindowRects()
{
    m_rcWindowClient.x = 0;
    m_rcWindowClient.y = 0;
    SDL_GetWindowSize(m_sdlWnd, &m_rcWindowClient.w, &m_rcWindowClient.h);

    SDL_GetWindowPosition(m_sdlWnd, &m_rcWindowBounds.x, &m_rcWindowBounds.y);
    SDL_GetWindowSize(m_sdlWnd, &m_rcWindowBounds.w, &m_rcWindowBounds.h);

    int top, left, bottom, right;
    SDL_GetWindowBordersSize(m_sdlWnd, &top, &left, &bottom, &right);
    m_rcWindowBounds.x -= left;
    m_rcWindowBounds.y -= top;
    m_rcWindowBounds.w += right;
    m_rcWindowBounds.h += bottom;
}

void CRenderDevice::SelectResolution(const bool windowed)
{
    // Dedicated server hardcoded resolution
    // XXX: to be removed
    if (GEnv.isDedicatedServer)
    {
        psDeviceMode.Width = 640;
        psDeviceMode.Height = 480;
    }
    else if (psDeviceMode.Width == 0 && psDeviceMode.Height == 0 && psDeviceMode.RefreshRate == 0)
    {
        SDL_DisplayMode current;
        SDL_GetCurrentDisplayMode(psDeviceMode.Monitor, &current);
        psDeviceMode.Width = current.w;
        psDeviceMode.Height = current.h;
        psDeviceMode.RefreshRate = current.refresh_rate;
    }
    else if (!windowed) // check if safe for fullscreen
    {
        string256 buf;
        xr_sprintf(buf, "%ux%u (%dHz)", psDeviceMode.Width, psDeviceMode.Height, psDeviceMode.RefreshRate);

        auto modes = vid_mode_token[psDeviceMode.Monitor];
        const auto it = std::find_if(modes.begin(), modes.end(), [&buf](const xr_token& token)
        {
            return token.name && xr_strcmp(token.name, buf) == 0;
        });

        if (it == modes.end()) // not found
        {
            SDL_DisplayMode current =
            {
                SDL_PIXELFORMAT_UNKNOWN,
                (int)psDeviceMode.Width,
                (int)psDeviceMode.Height,
                (int)psDeviceMode.RefreshRate,
                nullptr
            };

            SDL_DisplayMode closest; // try closest or fallback to desktop mode
            if (!SDL_GetClosestDisplayMode(psDeviceMode.Monitor, &current, &closest))
            {
                SDL_GetCurrentDisplayMode(psDeviceMode.Monitor, &closest);
            }

            psDeviceMode.Width = closest.w;
            psDeviceMode.Height = closest.h;
            psDeviceMode.RefreshRate = closest.refresh_rate;
        }
    }

    // Clamp the user-requested resolution to what actually fits on the
    // display in points. macOS otherwise silently shrinks the window after
    // SDL_SetWindowSize succeeds (e.g. a vid_mode of 2560x1600 ends up as
    // 1680x907 on an M1 Air at "More Space" scaling), and the RT
    // allocation we do later from psDeviceMode + drawable becomes 2x larger
    // than the actual default framebuffer — the engine then renders into a
    // bottom-left quadrant of the visible area. Doing it here keeps the
    // engine's state (psDeviceMode, RT sizes, drawable size, blit math) in
    // agreement instead of relying on a post-resize SIZE_CHANGED round-trip
    // that only fires after the first broken frame.
    if (windowed && !GEnv.isDedicatedServer)
    {
        SDL_Rect usable, full;
        const bool gotUsable = SDL_GetDisplayUsableBounds(psDeviceMode.Monitor, &usable) == 0
            && usable.w > 0 && usable.h > 0;
        const bool gotFull = SDL_GetDisplayBounds(psDeviceMode.Monitor, &full) == 0
            && full.w > 0 && full.h > 0;
        Msg("* SelectResolution: requested=%ux%u monitor=%u usable=%dx%d full=%dx%d",
            psDeviceMode.Width, psDeviceMode.Height, psDeviceMode.Monitor,
            gotUsable ? usable.w : -1, gotUsable ? usable.h : -1,
            gotFull ? full.w : -1, gotFull ? full.h : -1);
        if (gotUsable)
        {
            if (psDeviceMode.Width > static_cast<u32>(usable.w))
                psDeviceMode.Width = static_cast<u32>(usable.w);
            if (psDeviceMode.Height > static_cast<u32>(usable.h))
                psDeviceMode.Height = static_cast<u32>(usable.h);
        }
        Msg("* SelectResolution: after clamp=%ux%u", psDeviceMode.Width, psDeviceMode.Height);
    }

    dwWidth = psDeviceMode.Width;
    dwHeight = psDeviceMode.Height;
}

SDL_Window* CRenderDevice::GetApplicationWindow()
{
    return m_sdlWnd;
}

void CRenderDevice::OnErrorDialog(bool beforeDialog)
{
    const bool restore = !beforeDialog;
    const bool needUpdateInput = pInput && pInput->IsExclusiveMode();

    if (restore)
        UpdateWindowProps();
    else
        SDL_SetWindowFullscreen(m_sdlWnd, SDL_FALSE);

    if (needUpdateInput)
        pInput->GrabInput(restore);
}

void CRenderDevice::OnFatalError()
{
    // make it sure window will hide in any way
    SDL_SetWindowFullscreen(m_sdlWnd, SDL_FALSE);
    SDL_SetWindowAlwaysOnTop(m_sdlWnd, SDL_FALSE);
    SDL_ShowWindow(m_sdlWnd);
    SDL_MinimizeWindow(m_sdlWnd);
    SDL_HideWindow(m_sdlWnd);
}
