#include "stdafx.h"

#ifdef IMGUI_ENABLE_VIEWPORTS
#   include <SDL_syswm.h>
#endif

#include <imgui_internal.h>

#if defined(XR_PLATFORM_APPLE)
// A.6.1 — ImGui clipboard goes through NSPasteboard via the shim symbols
// exported from xrCore (see os_clipboard_mac.mm). Declared locally to avoid
// pulling AppKit into this TU or introducing an xrEngine->xrCore header.
extern "C" const char *OpenXRay_PasteboardGetString(void);
extern "C" bool        OpenXRay_PasteboardSetString(const char *utf8);

#   include "native_window.h"
#endif

void CRenderDevice::InitializeImGui()
{
    if (m_imgui_context)
        return;

    ZoneScoped;

    IMGUI_CHECKVERSION();

    ImGui::SetAllocatorFunctions(
        [](size_t size, void* /*user_data*/)
        {
            return xr_malloc(size);
        },
        [](void* ptr, void* /*user_data*/)
        {
            xr_free(ptr);
        }
    );
    m_imgui_context = ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();

    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard |
                      ImGuiConfigFlags_NavEnableGamepad |
                      ImGuiConfigFlags_DockingEnable;

    io.ConfigNavMoveSetMousePos = true;

    string_path fName;
    FS.update_path(fName, "$app_data_root$", io.IniFilename);
    convert_path_separators(fName);
    io.IniFilename = xr_strdup(fName);

    FS.update_path(fName, "$logs$", io.LogFilename);
    io.LogFilename = xr_strdup(fName);

    io.BackendPlatformName = "OpenXRay";

#if defined(XR_PLATFORM_APPLE)
    // ImGui ships with ProggyClean which is ASCII only. Debug overlays
    // that touch any cyrillic string (NPC names, save names, localized
    // labels surfaced by the same UTF-8 pipeline as the in-game text)
    // would otherwise render as boxes. macOS bundles Arial.ttf system-
    // wide; we point ImGui at it and explicitly merge ASCII + Cyrillic
    // glyph ranges so the atlas covers what the engine actually uses.
    {
        static const ImWchar ranges[] = {
            0x0020, 0x00FF, // Basic Latin + Latin-1 Supplement
            0x0400, 0x04FF, // Cyrillic
            0x2010, 0x2030, // Punctuation (em-dash, hellip, ...)
            0x20AC, 0x20AC, // €
            0x2116, 0x2116, // №
            0,
        };
        static const char* mac_arial = "/System/Library/Fonts/Supplemental/Arial.ttf";
        if (!io.Fonts->AddFontFromFileTTF(mac_arial, 14.0f, nullptr, ranges))
        {
            Msg("! ImGui: failed to load %s, falling back to ProggyClean (no cyrillic)", mac_arial);
            io.Fonts->AddFontDefault();
        }
    }
#endif

    io.ConfigDebugIsDebuggerPresent = xrDebug::DebuggerIsPresent();
#ifdef DEBUG
    io.ConfigErrorRecoveryEnableAssert  = true;
    io.ConfigErrorRecoveryEnableTooltip = true;
    io.ConfigDebugHighlightIdConflicts  = true;
#else
    io.ConfigErrorRecoveryEnableAssert  = false;
    io.ConfigErrorRecoveryEnableTooltip = false;
    io.ConfigDebugHighlightIdConflicts  = false;
#endif

    // Register platform interface (will be coupled with a renderer interface)
    ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();

    // Clipboard functionality
    platform_io.Platform_SetClipboardTextFn = [](ImGuiContext*, const char* text)
    {
#if defined(XR_PLATFORM_APPLE)
        OpenXRay_PasteboardSetString(text);
#else
        SDL_SetClipboardText(text);
#endif
    };

    platform_io.Platform_GetClipboardTextFn = [](ImGuiContext* ctx) -> const char*
    {
#if defined(XR_PLATFORM_APPLE)
        // NSPasteboard shim returns a pointer to a file-static xr_string that
        // stays valid until the next OpenXRay_PasteboardGetString call — which
        // matches ImGui's Platform_GetClipboardTextFn lifetime contract. No
        // caller-side free needed (and Platform_ClipboardUserData is unused).
        (void)ctx;
        return OpenXRay_PasteboardGetString();
#else
        ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO(ctx);
        auto clipboard_text_data = static_cast<char*>(platform_io.Platform_ClipboardUserData);

        if (clipboard_text_data)
            SDL_free(clipboard_text_data);

        clipboard_text_data = SDL_GetClipboardText();

        return clipboard_text_data;
#endif
    };

    platform_io.Platform_SetImeDataFn = [](ImGuiContext* ctx, ImGuiViewport* viewport, ImGuiPlatformImeData* data)
    {
        if (data->WantVisible)
        {
            const SDL_Rect r
            {
                .x = (int)(data->InputPos.x - viewport->Pos.x),
                .y = (int)(data->InputPos.y - viewport->Pos.y + data->InputLineHeight),
                .w = 1,
                .h = (int)data->InputLineHeight,
            };
            SDL_SetTextInputRect(&r);
        }
    };

#ifdef IMGUI_ENABLE_VIEWPORTS
    platform_io.Platform_CreateWindow = [](ImGuiViewport* viewport)
    {
        Uint32 sdl_flags{};
        GEnv.Render->ObtainRequiredWindowFlags(sdl_flags);

        //sdl_flags |= SDL_GetWindowFlags(bd->Window) & SDL_WINDOW_ALLOW_HIGHDPI; // XXX: high DPI
        sdl_flags |= SDL_WINDOW_HIDDEN;
        sdl_flags |= (viewport->Flags & ImGuiViewportFlags_NoDecoration) ? SDL_WINDOW_BORDERLESS : 0;
        sdl_flags |= (viewport->Flags & ImGuiViewportFlags_NoDecoration) ? 0 : SDL_WINDOW_RESIZABLE;
#if !defined(XR_PLATFORM_WINDOWS)
        // See SDL hack in ImGui_ImplSDL2_ShowWindow().
        sdl_flags |= (viewport->Flags & ImGuiViewportFlags_NoTaskBarIcon) ? SDL_WINDOW_SKIP_TASKBAR : 0;
#endif
        sdl_flags |= (viewport->Flags & ImGuiViewportFlags_TopMost) ? SDL_WINDOW_ALWAYS_ON_TOP : 0;

        const auto vd = IM_NEW(ImGuiViewportData)
        {
            viewport->Pos, viewport->Size, sdl_flags
        };
        viewport->PlatformUserData = vd;

        viewport->PlatformHandle = vd->Window;
        viewport->PlatformHandleRaw = nullptr;

        SDL_SysWMinfo info;
        SDL_VERSION(&info.version);
        if (SDL_GetWindowWMInfo(vd->Window, &info))
        {
#if defined(XR_PLATFORM_WINDOWS) && defined(SDL_VIDEO_DRIVER_WINDOWS)
            viewport->PlatformHandleRaw = info.info.win.window;
#elif defined(__APPLE__) && defined(SDL_VIDEO_DRIVER_COCOA)
            viewport->PlatformHandleRaw = (void*)info.info.cocoa.window;
#endif
        }

        if (viewport->ParentViewportId)
        {
            const auto parentViewport = ImGui::FindViewportByID(viewport->ParentViewportId);
            SDL_SetWindowModalFor(vd->Window, (SDL_Window*)parentViewport->PlatformHandle);
        }
    };

    platform_io.Platform_DestroyWindow = [](ImGuiViewport* viewport)
    {
        if (const auto vd = static_cast<ImGuiViewportData*>(viewport->PlatformUserData))
        {
            IM_DELETE(vd);
        }
        viewport->PlatformUserData = nullptr;
        viewport->PlatformHandle = nullptr;
        viewport->PlatformHandleRaw = nullptr;
    };

    platform_io.Platform_ShowWindow = [](ImGuiViewport* viewport)
    {
        const auto vd = static_cast<ImGuiViewportData*>(viewport->PlatformUserData);
#if defined(XR_PLATFORM_APPLE)
        // A.7.4 C.4a (gitea #192): main viewport на native path — vd->Window=nullptr;
        // окно уже видно (OpenXRay_NativeWindow_Show в Device_Initialize). Show
        // — no-op для main viewport, secondary viewports keep SDL path.
        if (!vd->Window)
            return;
#endif
#if defined(XR_PLATFORM_WINDOWS)
        const HWND hwnd = static_cast<HWND>(viewport->PlatformHandleRaw);

        // SDL hack: Hide icon from task bar
        // Note: SDL 2.0.6+ has a SDL_WINDOW_SKIP_TASKBAR flag which is supported under Windows but the way it create the window breaks our seamless transition.
        if (viewport->Flags & ImGuiViewportFlags_NoTaskBarIcon)
        {
            LONG ex_style = ::GetWindowLong(hwnd, GWL_EXSTYLE);
            ex_style &= ~WS_EX_APPWINDOW;
            ex_style |= WS_EX_TOOLWINDOW;
            ::SetWindowLong(hwnd, GWL_EXSTYLE, ex_style);
        }

        // SDL hack: SDL always activate/focus windows :/
        if (viewport->Flags & ImGuiViewportFlags_NoFocusOnAppearing)
        {
            ::ShowWindow(hwnd, SW_SHOWNA);
            return;
        }
#endif
        SDL_ShowWindow(vd->Window);
    };

    platform_io.Platform_SetWindowPos = [](ImGuiViewport* viewport, ImVec2 pos)
    {
        const auto vd = static_cast<ImGuiViewportData*>(viewport->PlatformUserData);
#if defined(XR_PLATFORM_APPLE)
        if (!vd->Window) return; // main viewport on native — position fixed by NSWindow
#endif
        SDL_SetWindowPosition(vd->Window, (int)pos.x, (int)pos.y);
    };

    platform_io.Platform_GetWindowPos = [](ImGuiViewport* viewport)
    {
        const auto vd = static_cast<ImGuiViewportData*>(viewport->PlatformUserData);
#if defined(XR_PLATFORM_APPLE)
        if (!vd->Window)
        {
            int x = 0, y = 0, w = 0, h = 0;
            OpenXRay_NativeWindow_GetFrameRect(&x, &y, &w, &h);
            return ImVec2{ (float)x, (float)y };
        }
#endif
        int x = 0, y = 0;
        SDL_GetWindowPosition(vd->Window, &x, &y);
        return ImVec2{ (float)x, (float)y };
    };

    platform_io.Platform_SetWindowSize = [](ImGuiViewport* viewport, ImVec2 size)
    {
        const auto vd = static_cast<ImGuiViewportData*>(viewport->PlatformUserData);
#if defined(XR_PLATFORM_APPLE)
        if (!vd->Window) return; // main viewport on native — size driven by NSWindow resize → C.4b
#endif
        SDL_SetWindowSize(vd->Window, (int)size.x, (int)size.y);
    };

    platform_io.Platform_GetWindowSize = [](ImGuiViewport* viewport)
    {
        const auto vd = static_cast<ImGuiViewportData*>(viewport->PlatformUserData);
#if defined(XR_PLATFORM_APPLE)
        if (!vd->Window)
        {
            int x = 0, y = 0, w = 0, h = 0;
            OpenXRay_NativeWindow_GetClientRect(&x, &y, &w, &h);
            return ImVec2{ (float)w, (float)h };
        }
#endif
        int w = 0, h = 0;
        SDL_GetWindowSize(vd->Window, &w, &h);
        return ImVec2{ (float)w, (float)h };
    };

    platform_io.Platform_SetWindowFocus = [](ImGuiViewport* viewport)
    {
        const auto vd = static_cast<ImGuiViewportData*>(viewport->PlatformUserData);
#if defined(XR_PLATFORM_APPLE)
        if (!vd->Window) return; // main viewport already key — see OpenXRay_NativeWindow_Show
#endif
        SDL_RaiseWindow(vd->Window);
    };

    platform_io.Platform_GetWindowFocus = [](ImGuiViewport* viewport)
    {
        const auto vd = static_cast<ImGuiViewportData*>(viewport->PlatformUserData);
#if defined(XR_PLATFORM_APPLE)
        if (!vd->Window)
            return OpenXRay_NativeWindow_IsKeyWindow();
#endif
        return (SDL_GetWindowFlags(vd->Window) & SDL_WINDOW_INPUT_FOCUS) != 0;
    };

    platform_io.Platform_GetWindowMinimized = [](ImGuiViewport* viewport)
    {
        const auto vd = static_cast<ImGuiViewportData*>(viewport->PlatformUserData);
#if defined(XR_PLATFORM_APPLE)
        if (!vd->Window)
            return OpenXRay_NativeWindow_IsMinimized();
#endif
        return (SDL_GetWindowFlags(vd->Window) & SDL_WINDOW_MINIMIZED) != 0;
    };

    platform_io.Platform_SetWindowTitle = [](ImGuiViewport* viewport, const char* title)
    {
        const auto vd = static_cast<ImGuiViewportData*>(viewport->PlatformUserData);
#if defined(XR_PLATFORM_APPLE)
        if (!vd->Window)
        {
            OpenXRay_NativeWindow_SetTitle(title);
            return;
        }
#endif
        SDL_SetWindowTitle(vd->Window, title);
    };

    platform_io.Platform_SetWindowAlpha = [](ImGuiViewport* viewport, float alpha)
    {
        const auto vd = static_cast<ImGuiViewportData*>(viewport->PlatformUserData);
#if defined(XR_PLATFORM_APPLE)
        if (!vd->Window) return; // main viewport opacity stays at 1.0 on native (C.4b helper)
#endif
        SDL_SetWindowOpacity(vd->Window, alpha);
    };
#endif // IMGUI_ENABLE_VIEWPORTS

    editor().InitBackend();

    if (io.BackendFlags & ImGuiBackendFlags_PlatformHasViewports)
        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
}

void CRenderDevice::DestroyImGui()
{
    if (!m_imgui_context)
        return;

    ZoneScoped;

    m_imgui_render->OnDeviceDestroy();
    GEnv.RenderFactory->DestroyImGuiRender(m_imgui_render);
    m_imgui_render = nullptr;

#ifdef IMGUI_ENABLE_VIEWPORTS
    ImGui::DestroyPlatformWindows();
#endif

    ImGuiIO& io = ImGui::GetIO();
    xr_free(io.IniFilename);
    xr_free(io.LogFilename);

    ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO(m_imgui_context);

    if (platform_io.Platform_ClipboardUserData)
    {
        SDL_free(platform_io.Platform_ClipboardUserData);
    }

    ImGui::DestroyContext(m_imgui_context);
    m_imgui_context = nullptr;
}
