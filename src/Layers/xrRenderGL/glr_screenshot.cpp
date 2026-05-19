#include "stdafx.h"

//#include "xr_effgamma.h"
#include "xrCore/Media/Image.hpp"
#include "xrEngine/xrImage_Resampler.h"

namespace xray::render::RENDER_NAMESPACE
{
using namespace XRay::Media;

#define GAMESAVE_SIZE 128

#define SM_FOR_SEND_WIDTH 640
#define SM_FOR_SEND_HEIGHT 480

// Minimal DDS header for uncompressed RGBA8 (no mipmaps, single 2D
// surface). gli (the GL DDS loader, see glTexture.cpp:111) accepts
// this layout; the legacy DX9 pipeline does too. We don't BC1-compress
// here because the savedgames overhead is ~3MB total at 64KB/save,
// negligible compared to writing a real encoder.
namespace
{
#pragma pack(push, 1)
struct DDS_PIXELFORMAT
{
    u32 dwSize;        // 32
    u32 dwFlags;       // DDPF_RGB | DDPF_ALPHAPIXELS
    u32 dwFourCC;      // 0 for uncompressed
    u32 dwRGBBitCount; // 32
    u32 dwRBitMask;
    u32 dwGBitMask;
    u32 dwBBitMask;
    u32 dwABitMask;
};
struct DDS_HEADER
{
    u32 dwMagic; // "DDS "
    u32 dwSize;  // 124
    u32 dwFlags; // CAPS|HEIGHT|WIDTH|PIXELFORMAT|PITCH
    u32 dwHeight;
    u32 dwWidth;
    u32 dwPitchOrLinearSize;
    u32 dwDepth;
    u32 dwMipMapCount;
    u32 dwReserved1[11];
    DDS_PIXELFORMAT ddspf;
    u32 dwCaps; // DDSCAPS_TEXTURE
    u32 dwCaps2;
    u32 dwCaps3;
    u32 dwCaps4;
    u32 dwReserved2;
};
#pragma pack(pop)
static_assert(sizeof(DDS_HEADER) == 128, "DDS header must be 4-byte magic + 124-byte struct");
} // namespace

// XXX: Provide full implementation
void CRender::Screenshot(ScreenshotMode mode /*= SM_NORMAL*/, pcstr name /*= nullptr*/)
{
    switch (mode)
    {
    case SM_NORMAL:
    {
        pcstr extension = "jpg";

        string64 time;
        string_path buf;
        xr_sprintf(buf, sizeof(buf), "ss_%s_%s_(%s).%s", Core.UserName, timestamp(time),
            g_pGameLevel ? g_pGameLevel->name().c_str() : "mainmenu", extension);

        IWriter* fs = FS.w_open("$screenshots$", buf);
        R_ASSERT(fs);

        xr_vector<u8> pixels;
        pixels.resize(Device.dwWidth * Device.dwHeight * 3);

        glReadPixels(0, 0, Device.dwWidth, Device.dwHeight, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());

        Image img{ Device.dwWidth, Device.dwHeight, pixels.data(), ImageDataFormat::RGB8 };
        if (!img.SaveJPEG(*fs, 100, true))
            Log("! Failed to make a screenshot.");

        FS.w_close(fs);
        break;
    }

    case SM_FOR_GAMESAVE:
    {
        if (!name)
            break;

        const u32 srcW = Device.dwWidth;
        const u32 srcH = Device.dwHeight;
        if (srcW == 0 || srcH == 0)
            break;

        // Read RGBA8 backbuffer.
        xr_vector<u32> src(srcW * srcH);
        glReadPixels(0, 0, srcW, srcH, GL_RGBA, GL_UNSIGNED_BYTE, src.data());

        // GL is bottom-up, DDS top-down -- swap rows in place.
        for (u32 y = 0; y < srcH / 2; ++y)
        {
            u32* row_top = src.data() + y * srcW;
            u32* row_bot = src.data() + (srcH - 1 - y) * srcW;
            for (u32 x = 0; x < srcW; ++x)
                std::swap(row_top[x], row_bot[x]);
        }

        // Box-filter resample to 128x128.
        xr_vector<u32> dst(GAMESAVE_SIZE * GAMESAVE_SIZE);
        imf_Process(dst.data(), GAMESAVE_SIZE, GAMESAVE_SIZE, src.data(), srcW, srcH, imf_box);

        // Build a legacy DDS header for uncompressed RGBA8 (R in low byte).
        DDS_HEADER hdr{};
        hdr.dwMagic = 0x20534444; // 'DDS '
        hdr.dwSize = 124;
        hdr.dwFlags = 0x00001007 | 0x00000008; // CAPS|HEIGHT|WIDTH|PIXELFORMAT | PITCH
        hdr.dwHeight = GAMESAVE_SIZE;
        hdr.dwWidth = GAMESAVE_SIZE;
        hdr.dwPitchOrLinearSize = GAMESAVE_SIZE * 4;
        hdr.ddspf.dwSize = 32;
        hdr.ddspf.dwFlags = 0x00000041; // DDPF_RGB | DDPF_ALPHAPIXELS
        hdr.ddspf.dwRGBBitCount = 32;
        hdr.ddspf.dwRBitMask = 0x000000FF;
        hdr.ddspf.dwGBitMask = 0x0000FF00;
        hdr.ddspf.dwBBitMask = 0x00FF0000;
        hdr.ddspf.dwABitMask = 0xFF000000;
        hdr.dwCaps = 0x00001000; // DDSCAPS_TEXTURE

        IWriter* fs = FS.w_open(name);
        if (!fs)
        {
            Msg("! Save thumbnail: cannot open '%s' for write", name);
            break;
        }
        fs->w(&hdr, sizeof(hdr));
        fs->w(dst.data(), dst.size() * sizeof(u32));
        FS.w_close(fs);
        break;
    }

    default:
        // XXX [ragnar] GL_SCREENSHOT_MODES: still missing for GL backend:
        //   SM_FOR_LEVELMAP   — top-down render-to-texture used by the
        //                       editor minimap exporter
        //   SM_FOR_CUBEMAP    — six-face capture for env probes (used by
        //                       cube-textured environment bakes)
        //   SM_FOR_SEND       — multiplayer screenshot upload (640x480
        //                       JPEG, never tested on macOS since MP is
        //                       disabled)
        // The DX11 backend implements all three in dx11r_screenshot.cpp.
        // None of these are blockers for single-player CoP playthrough.
        VERIFY(!"CRender::Screenshot. This screenshot type is not supported for OGL.");
    }
}
} // namespace xray::render::RENDER_NAMESPACE
