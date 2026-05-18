#pragma once

#include "xrEngine/IGameFont.hpp"
#include "xrCommon/xr_vector.h"
#include "xrCommon/xr_unordered_map.h"
#include "xrCore/_vector3d.h"
#ifdef DEBUG
#include "xrCore/xrstring.h"
#endif

namespace xray::render
{
namespace render_r4
{
class dxFontRender;
}
namespace render_gl
{
class dxFontRender;
}
} // namespace xray::render

class IFontRender;

class ENGINE_API CGameFont final : public IGameFont
{
    friend class xray::render::render_r4::dxFontRender;
    friend class xray::render::render_gl::dxFontRender;

private:
    struct String
    {
        string1024 string;
        float x, y;
        float height;
        u32 c;
        EAligment align;
    };

protected:
    Fvector2 vHalfPixel;
    Ivector2 vTS;
    EAligment eCurrentAlignment;
    u8 uFlags;
    u32 dwCurrentColor;
    float fCurrentHeight;
    float fCurrentX, fCurrentY;
    Fvector2 vInterval;
    float fHeight;
    float fXStep;
    float fYStep;
    float fTCHeight;
    u32 nNumChars{ 256 };
    xr_vector<String> strings;
    Fvector* TCMap{};
    IFontRender* pFontRender;

    // Maps a Unicode codepoint to the atlas slot index in TCMap. Built at
    // load time:
    //   - single-byte fonts: iterates 0..255 and inserts
    //     xr_cp1251_to_unicode[i] -> i; this is what makes UTF-8 input
    //     resolvable against a font atlas authored as cp1251 byte indices.
    //   - multi-byte fonts: left empty; their TCMap is already keyed by
    //     BMP codepoint, so SlotForCodepoint returns the codepoint directly.
    //
    // Used only when s_utf8_mode is enabled (Phase 1 final step). With the
    // flag off, the legacy byte-indexed rendering path is unchanged.
    xr_unordered_map<u32, u16> m_codepointToSlot;

protected:
    const Fvector& GetCharTC(u16 c) const { return TCMap[c]; }

    // Resolve a Unicode codepoint to a TCMap slot. Returns '?' (0x3F) as a
    // visible fallback when the font lacks a glyph for the codepoint.
    u16 SlotForCodepoint(u32 cp) const;

public:
    // Engine-wide toggle for the codepoint-aware rendering path. Default
    // false during Phase 1 buildup; flipped to true once every renderer
    // call site is migrated (Phase 1 Step 1.8). Static so test fixtures
    // and the future r__utf8_mode console var can flip it uniformly.
    static bool s_utf8_mode;

public:
    CGameFont(pcstr section, u8 flags = 0);
    CGameFont(pcstr shader, pcstr texture, u8 flags = 0);
    virtual ~CGameFont();

    virtual void Initialize(pcstr shader, pcstr texture) override;
    virtual void SetColor(u32 C) override { dwCurrentColor = C; }
    virtual u32 GetColor() const override { return dwCurrentColor; }
    virtual void SetHeightI(float S) override;
    virtual void SetHeight(float S) override;
    virtual float GetHeight() const override { return fCurrentHeight; };
    virtual void SetInterval(float x, float y) override { vInterval.set(x, y); };
    virtual void SetInterval(const Fvector2& v) override { vInterval.set(v); };
    virtual void SetAligment(EAligment aligment) override { eCurrentAlignment = aligment; }
    virtual float SizeOf_(pcstr s) override;
    virtual float SizeOf_(const xr_wide_char* wsStr) override;
    virtual float SizeOf_(const char cChar); // only ANSII
    virtual float CurrentHeight_() override;
    virtual void OutSetI(float x, float y) override;
    virtual void OutSet(float x, float y) override;
    virtual Fvector2 GetPosition() const override { return {fCurrentX, fCurrentY}; }
    virtual void MasterOut(bool bCheckDevice, bool bUseCoords, bool bScaleCoords, bool bUseSkip, float _x, float _y,
        float _skip, pcstr fmt, va_list p) override;
    virtual u32 smart_strlen(pcstr S) override;
    virtual std::pair<u32, u32> get_actions_text_length(pcstr s) override; // returns count of actions plus total text size of all actions
    virtual bool IsMultibyte() const override { return (uFlags & fsMultibyte); };
    virtual u16 SplitByWidth(u16* puBuffer, u16 uBufferSize, float fTargetWidth, pcstr pszText) override;
    virtual u16 GetCutLengthPos(float fTargetWidth, pcstr pszText) override;
    virtual void OutI(float _x, float _y, pcstr fmt, ...) override;
    virtual void Out(float _x, float _y, pcstr fmt, ...) override;
    virtual void OutNext(pcstr fmt, ...) override;
    virtual void OutNextVA(pcstr format, va_list args) override;
    virtual void OutSkip(float val = 1.f) override;
    virtual void OnRender() override;
    virtual void Clear() override { strings.clear(); }
#ifdef DEBUG
    shared_str m_font_name;
#endif
};
