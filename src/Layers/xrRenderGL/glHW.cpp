// glHW.cpp: implementation of the OpenGL specialisation of CHW.
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#pragma hdrstop

#include "glHW.h"
#include "xrEngine/XR_IOConsole.h"

#if defined(XR_PLATFORM_APPLE)
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
// A.7.4-restart Step 3 (gitea #186): pure observability на render swap
// pipeline. Эти DLOG'и + опциональный red-clear probe не меняют поведение
// рендера — только дают чистый сигнал на каком фрейме / в каком состоянии
// GL находится перед blit'ом RT в FBO 0 и перед SDL_GL_SwapWindow. После
// того как у нас будет trace engine'ового render pipeline на текущем
// (SDL) пути, step 4 будет переключать swap path на NSOpenGLContext'овый
// flushBuffer с уже понятыми инвариантами.
//
// Red-clear probe (опциональный, gated env var OPENXRAY_RED_CLEAR_PROBE=1):
// после blit'а RT в FBO 0, ПЕРЕД SDL_GL_SwapWindow перезаписываем FBO 0
// чистым красным. Если экран краснеет на A.7.4b будущем native swap'е —
// значит swap path физически работает с правильным FBO. Если чёрный —
// значит swap не идёт через FBO 0 или мы binds не туда.
#   define A74P_RENDER_DLOG(fmt, ...) do {                                      \
        char _buf[512];                                                         \
        int _n = snprintf(_buf, sizeof _buf,                                    \
                          "==> a74p[render:%s]: " fmt "\n",                     \
                          __func__, ##__VA_ARGS__);                             \
        if (_n > 0)                                                             \
            (void)::write(STDERR_FILENO, _buf, (size_t)_n);                     \
    } while (0)

namespace
{
const char* GlErrorName(GLenum e)
{
    switch (e)
    {
    case GL_NO_ERROR:                      return "GL_NO_ERROR";
    case GL_INVALID_ENUM:                  return "GL_INVALID_ENUM";
    case GL_INVALID_VALUE:                 return "GL_INVALID_VALUE";
    case GL_INVALID_OPERATION:             return "GL_INVALID_OPERATION";
    case GL_INVALID_FRAMEBUFFER_OPERATION: return "GL_INVALID_FRAMEBUFFER_OPERATION";
    case GL_OUT_OF_MEMORY:                 return "GL_OUT_OF_MEMORY";
    default:                               return "?";
    }
}
} // namespace
#else
#   define A74P_RENDER_DLOG(fmt, ...) ((void)0)
#endif

namespace xray::render::RENDER_NAMESPACE
{
CHW HW;

void CALLBACK OnDebugCallback(GLenum /*source*/, GLenum /*type*/, GLuint id, GLenum severity, GLsizei /*length*/,
    const GLchar* message, const void* /*userParam*/)
{
    if (severity != GL_DEBUG_SEVERITY_NOTIFICATION)
        Log(message, id);
}

static_assert(std::is_same_v<decltype(&OnDebugCallback), GLDEBUGPROC>);

void UpdateVSync()
{
    if (psDeviceFlags.test(rsVSync))
    {
        // Try adaptive vsync first
        if (SDL_GL_SetSwapInterval(-1) == -1)
            SDL_GL_SetSwapInterval(1);
    }
    else
    {
        SDL_GL_SetSwapInterval(0);
    }
}

CHW::CHW()
{
    if (!ThisInstanceIsGlobal())
        return;

    Device.seqAppActivate.Add(this);
    Device.seqAppDeactivate.Add(this);
}

CHW::~CHW()
{
    if (!ThisInstanceIsGlobal())
        return;

    Device.seqAppActivate.Remove(this);
    Device.seqAppDeactivate.Remove(this);
}

void CHW::OnAppActivate()
{
    if (m_window)
    {
        SDL_RestoreWindow(m_window);
    }
}

void CHW::OnAppDeactivate()
{
    if (m_window)
    {
        if (psDeviceMode.WindowStyle == rsFullscreen || psDeviceMode.WindowStyle == rsFullscreenBorderless)
            SDL_MinimizeWindow(m_window);
    }
}

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////
void CHW::CreateDevice(SDL_Window* hWnd)
{
    ZoneScoped;

    m_window = hWnd;

    R_ASSERT(m_window);

    // Choose the closest pixel format
    SDL_DisplayMode mode;
    SDL_GetWindowDisplayMode(m_window, &mode);
    mode.format = SDL_PIXELFORMAT_RGBA8888;
    // Apply the pixel format to the device context
    SDL_SetWindowDisplayMode(m_window, &mode);

    Caps.fTarget = D3DFMT_A8R8G8B8;
    Caps.fDepth = D3DFMT_D24S8;

    // Create the context
    m_context = SDL_GL_CreateContext(m_window);
    if (m_context == nullptr)
    {
        Log("! OpenGL: could not create drawing context:", SDL_GetError());
        return;
    }

    if (MakeContextCurrent(IRender::PrimaryContext) != 0)
    {
        Log("! OpenGL: could not make context current:", SDL_GetError());
        return;
    }

    int version;
    {
        ZoneScopedN("gladLoadGL");
        version = gladLoadGL(reinterpret_cast<GLADloadfunc>(SDL_GL_GetProcAddress));
    }
    if (version == 0)
    {
        Log("! OpenGL: could not initialize GLAD.");
        if (const auto err = SDL_GetError())
            Log("SDL Error:", err);
        return;
    }

    if (ThisInstanceIsGlobal())
    {
        UpdateVSync();

#ifdef DEBUG
        if (glDebugMessageCallback)
        {
            CHK_GL(glEnable(GL_DEBUG_OUTPUT));
            CHK_GL(glDebugMessageCallback((GLDEBUGPROC)OnDebugCallback, nullptr));
        }
#endif // DEBUG
    }

    int iMaxVTFUnits, iMaxCTIUnits;
    glGetIntegerv(GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS, &iMaxVTFUnits);
    glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &iMaxCTIUnits);

    AdapterName = reinterpret_cast<pcstr>(glGetString(GL_RENDERER));
    OpenGLVersionString = reinterpret_cast<pcstr>(glGetString(GL_VERSION));
    ShadingVersion = reinterpret_cast<pcstr>(glGetString(GL_SHADING_LANGUAGE_VERSION));

    Msg("* GPU vendor: [%s] device: [%s]", glGetString(GL_VENDOR), AdapterName);
    Msg("* GPU OpenGL version: %s", OpenGLVersionString);
    Msg("* GPU OpenGL shading language version: %s", ShadingVersion);
    Msg("* GPU OpenGL VTF units: [%d] CTI units: [%d]", iMaxVTFUnits, iMaxCTIUnits);

    ComputeShadersSupported = false; // XXX: Implement compute shaders support

    // Core profile requires a non-zero VAO bound for every draw. Engine flow
    // binds per-format VAOs via set_Format, but any draw issued before the
    // first set_Format (or after frame-end Invalidate clears the cache) hit
    // a GL_INVALID_OPERATION storm on Apple GL 4.1. A persistent default VAO
    // gives a known-good fallback.
    glGenVertexArrays(1, &m_defaultVAO);
    CHK_GL(glBindVertexArray(m_defaultVAO));

    // One-shot startup diagnostic for HiDPI / viewport mismatch debugging.
    // Reports point dims vs drawable dims vs default-framebuffer size and
    // whether the SDL window picked up SDL_WINDOW_ALLOW_HIGHDPI. Goes to
    // stdout (captured by launcher even on hard-exit) since the engine log
    // FILE* buffer is lost when the player double-Cmd+Q's a broken frame.
    if (m_window)
    {
        int wPts = 0, hPts = 0, wPx = 0, hPx = 0;
        SDL_GetWindowSize(m_window, &wPts, &hPts);
        SDL_GL_GetDrawableSize(m_window, &wPx, &hPx);
        const Uint32 winFlags = SDL_GetWindowFlags(m_window);
        GLint fbBinding = -1, viewport[4] = {0, 0, 0, 0};
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &fbBinding);
        glGetIntegerv(GL_VIEWPORT, viewport);
        Msg("* GL surface: window=%dx%d drawable=%dx%d allow_hidpi=%d "
            "fb_binding=%d viewport=%dx%d default_vao=%u",
            wPts, hPts, wPx, hPx,
            (winFlags & SDL_WINDOW_ALLOW_HIGHDPI) ? 1 : 0,
            (int)fbBinding, viewport[2], viewport[3],
            (unsigned)m_defaultVAO);
    }

    if (glGenFramebuffers && glBindFramebuffer)
        UpdateViews();
}

void CHW::DestroyDevice()
{
    if (m_defaultVAO)
    {
        glDeleteVertexArrays(1, &m_defaultVAO);
        m_defaultVAO = 0;
    }

    CHK_GL(glDeleteFramebuffers(1, &pFB));
    pFB = 0;

    const auto context = SDL_GL_GetCurrentContext();
    if (context == m_context)
        SDL_GL_MakeCurrent(nullptr, nullptr);

    SDL_GL_DeleteContext(m_context);
    m_context = nullptr;
}

//////////////////////////////////////////////////////////////////////
// Resetting device
//////////////////////////////////////////////////////////////////////
void CHW::Reset()
{
    ZoneScoped;

    CHK_GL(glDeleteFramebuffers(1, &pFB));
    pFB = 0;
    UpdateViews();

    UpdateVSync();
}

void CHW::SetPrimaryAttributes(u32& windowFlags)
{
    windowFlags |= SDL_WINDOW_OPENGL;

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);

    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    if (!strstr(Core.Params, "-no_gl_context"))
    {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    }
}

IRender::RenderContext CHW::GetCurrentContext() const
{
    const auto context = SDL_GL_GetCurrentContext();
    if (context == m_context)
        return IRender::PrimaryContext;
    return IRender::NoContext;
}

int CHW::MakeContextCurrent(IRender::RenderContext context) const
{
    switch (context)
    {
    case IRender::NoContext:
        return SDL_GL_MakeCurrent(nullptr, nullptr);

    case IRender::PrimaryContext:
        return SDL_GL_MakeCurrent(m_window, m_context);

    default:
        NODEFAULT;
    }
    return -1;
}

void CHW::UpdateViews()
{
    // Create the default framebuffer
    glGenFramebuffers(1, &pFB);
    CHK_GL(glBindFramebuffer(GL_FRAMEBUFFER, pFB));

    BackBufferCount = 1;
}

void CHW::BeginScene() { }
void CHW::EndScene() { }

void CHW::Present()
{
    // One-shot diagnostic: log src (Device.dwWidth/dwHeight, RT-attached) vs
    // dest (SDL drawable) on the first Present so we can confirm whether the
    // blit math matches the default framebuffer's actual backing size.
    static bool s_loggedFirst = false;
    if (!s_loggedFirst)
    {
        s_loggedFirst = true;
        int pxW = 0, pxH = 0;
        SDL_GL_GetDrawableSize(m_window, &pxW, &pxH);
        Msg("* Present[0]: src=%ux%u dest=%ux%u drawable=%dx%d pFB=%u",
            Device.dwWidth, Device.dwHeight,
            Device.dwWidth, Device.dwHeight,
            pxW, pxH, (unsigned)pFB);
    }

#if defined(XR_PLATFORM_APPLE)
    // A.7.4-restart Step 3 (gitea #186): подробный trace render swap pipeline.
    // Первые 3 Present'а дампим полный GL state до и после blit'а; затем
    // periodic GL error pulse каждые ~600 frames (10 сек @ 60fps) ловит
    // sticky errors на длинном run. Red-clear probe — опциональный
    // sanity-test default framebuffer (env var OPENXRAY_RED_CLEAR_PROBE=1).
    static unsigned   s_frameIdx       = 0;
    static const bool s_redClearProbe  = ::getenv("OPENXRAY_RED_CLEAR_PROBE") != nullptr;
    const bool        verbose          = (s_frameIdx < 3);
    const bool        errorPulse       = (s_frameIdx > 0) && (s_frameIdx % 600 == 0);

    if (s_frameIdx == 0)
        A74P_RENDER_DLOG("OPENXRAY_RED_CLEAR_PROBE=%d (1=overwrite FBO 0 red before swap)",
                          s_redClearProbe ? 1 : 0);

    if (verbose)
    {
        GLint preDrawFB = -1, preReadFB = -1, viewport[4] = {0,0,0,0};
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &preDrawFB);
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &preReadFB);
        glGetIntegerv(GL_VIEWPORT, viewport);
        A74P_RENDER_DLOG("frame[%u] PRE-blit: pFB=%u draw_fb=%d read_fb=%d viewport=%d,%d,%d,%d",
                          s_frameIdx, (unsigned)pFB, (int)preDrawFB, (int)preReadFB,
                          viewport[0], viewport[1], viewport[2], viewport[3]);
        // Drain pre-existing GL errors so post-blit/post-swap дампит ТОЛЬКО
        // ошибки этого frame'а.
        for (int i = 0; i < 10; ++i)
        {
            const GLenum err = glGetError();
            if (err == GL_NO_ERROR) break;
            A74P_RENDER_DLOG("frame[%u] PRE-blit drain GL error %d=0x%x (%s)",
                              s_frameIdx, i, (unsigned)err, GlErrorName(err));
        }
    }
#endif

#if 0 // kept for historical reasons
    RImplementation.Target->phase_flip();
#else
    glBindFramebuffer(GL_READ_FRAMEBUFFER, pFB);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glBlitFramebuffer(
        0, 0, Device.dwWidth, Device.dwHeight,
        0, 0, Device.dwWidth, Device.dwHeight,
        GL_COLOR_BUFFER_BIT, GL_NEAREST);
#endif

#if defined(XR_PLATFORM_APPLE)
    if (verbose)
    {
        // Post-blit GL state + error check.
        GLint postDrawFB = -1, postReadFB = -1;
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &postDrawFB);
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &postReadFB);
        const GLenum fbStatusDraw = glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER);
        const GLenum fbStatusRead = glCheckFramebufferStatus(GL_READ_FRAMEBUFFER);
        A74P_RENDER_DLOG("frame[%u] POST-blit: draw_fb=%d read_fb=%d draw_status=0x%x read_status=0x%x",
                          s_frameIdx, (int)postDrawFB, (int)postReadFB,
                          (unsigned)fbStatusDraw, (unsigned)fbStatusRead);
        for (int i = 0; i < 10; ++i)
        {
            const GLenum err = glGetError();
            if (err == GL_NO_ERROR) break;
            A74P_RENDER_DLOG("frame[%u] POST-blit GL error %d=0x%x (%s)",
                              s_frameIdx, i, (unsigned)err, GlErrorName(err));
        }
    }

    if (s_redClearProbe)
    {
        // Override FBO 0 red right before swap. Engine рендер всё равно
        // отойдёт на следующий frame, так что цена эксперимента — один
        // красный кадр когда probe включён.
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        if (verbose)
            A74P_RENDER_DLOG("frame[%u] RED-CLEAR probe applied to FBO 0", s_frameIdx);
    }

    if (errorPulse)
    {
        bool any = false;
        for (int i = 0; i < 10; ++i)
        {
            const GLenum err = glGetError();
            if (err == GL_NO_ERROR) break;
            any = true;
            A74P_RENDER_DLOG("frame[%u] PULSE GL error %d=0x%x (%s)",
                              s_frameIdx, i, (unsigned)err, GlErrorName(err));
        }
        if (!any)
            A74P_RENDER_DLOG("frame[%u] PULSE: GL error queue clean", s_frameIdx);
    }
#endif

    SDL_GL_SwapWindow(m_window);
    CurrentBackBuffer = (CurrentBackBuffer + 1) % BackBufferCount;

#if defined(XR_PLATFORM_APPLE)
    if (verbose)
        A74P_RENDER_DLOG("frame[%u] POST-swap: done", s_frameIdx);
    ++s_frameIdx;
#endif
}

DeviceState CHW::GetDeviceState() const
{
    //  TODO: OGL: Implement GetDeviceState
    return DeviceState::Normal;
}

std::pair<u32, u32> CHW::GetSurfaceSize() const
{
    // Drawable size is in physical pixels — on HiDPI / Retina that's 2x the
    // window points the user picked in vid_mode. The render path (RT
    // allocation, glViewport, Present blit) needs pixels; mouse / UI input
    // continues to use point dims via SDL_GetWindowSize elsewhere.
    if (m_window)
    {
        int w = 0, h = 0;
        SDL_GL_GetDrawableSize(m_window, &w, &h);
        if (w > 0 && h > 0)
            return { static_cast<u32>(w), static_cast<u32>(h) };
    }
    return { psDeviceMode.Width, psDeviceMode.Height };
}

bool CHW::ThisInstanceIsGlobal() const
{
    return this == &HW;
}

void CHW::BeginPixEvent(pcstr name) const
{
    if (glPushDebugGroup)
        glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, name);
}

void CHW::EndPixEvent() const
{
    if (glPushDebugGroup)
        glPopDebugGroup();
}
} // namespace xray::render::RENDER_NAMESPACE
