#pragma once
#include "xrCore/xrCore.h"

typedef u16 xr_wide_char; // Don't replace it with wchar_t, it won't work correctly, I don't know why.

// Full Unicode codepoint type for UTF-8-aware code paths. The renderer
// rewrite (utf8 migration Phase 1) iterates over codepoints, not over
// xr_wide_char which is restricted to the BMP (U+0000..U+FFFF). Use this
// in any new code that decodes UTF-8 from byte streams.
typedef u32 xr_codepoint;

// U+FFFD REPLACEMENT CHARACTER — the value xr_decode_utf8 returns when it
// encounters a malformed byte sequence, per the Unicode standard's
// recommendation for "best practices for substitution of maximal subparts."
inline constexpr xr_codepoint XR_UNICODE_REPLACEMENT = 0xFFFD;

#define MAX_MB_CHARS 4096

XRCORE_API u16 mbhMulti2Wide(xr_wide_char* WideStr, xr_wide_char* WidePos, u16 WideStrSize, const char* MultiStr);

// Decode the next UTF-8 codepoint from `p` and advance `p` past it. Returns
// the decoded codepoint in `cp` and true on success, or U+FFFD + false on
// malformed input (in which case `p` advances by one byte to make forward
// progress safe in a tight loop).
//
// Empty input (`*p == 0`) returns false with cp == 0 and leaves p unchanged.
//
// The decoder is strict per RFC 3629: rejects overlong forms, surrogate
// halves, and codepoints above U+10FFFF. Matches the validation done by
// xr_is_valid_utf8 — what passes the validator decodes here, what fails
// the validator yields U+FFFD here.
//
// Used by:
//   - Phase 1 font renderer (replaces byte-indexed cp1251 single-byte path)
//   - line_edit_control cursor arithmetic (advances by codepoint, not byte)
//   - CUILines word-wrap (width measured per codepoint glyph)
XRCORE_API bool xr_decode_utf8(const char*& p, xr_codepoint& cp);

// True if `b` is a UTF-8 continuation byte (10xxxxxx, 0x80..0xBF). The
// line-edit cursor uses this to skip past continuation bytes so it never
// lands mid-codepoint.
inline bool xr_utf8_is_continuation(unsigned char b) { return (b & 0xC0) == 0x80; }

// Length in bytes of the UTF-8 codepoint starting at lead byte `b`. Returns
// 1 for ASCII or for malformed leads (so the caller still makes progress
// and doesn't deadlock on a stray continuation byte). Cursor right-arrow
// uses this to skip the entire codepoint in one step.
inline size_t xr_utf8_lead_size(unsigned char b)
{
    if (b < 0x80) return 1;
    if ((b & 0xE0) == 0xC0) return 2;
    if ((b & 0xF0) == 0xE0) return 3;
    if ((b & 0xF8) == 0xF0) return 4;
    return 1; // continuation byte or 5/6-byte forbidden lead: step 1 to make progress
}

IC bool IsNeedSpaceCharacter(xr_wide_char wc)
{
    return ((wc == 0x0020) || (wc == 0x3000) || (wc == 0xFF01) || (wc == 0xFF0C) ||
        // ( wc == 0xFF0D ) ||
        (wc == 0xFF0E) || (wc == 0xFF1A) || (wc == 0xFF1B) || (wc == 0xFF1F) || (wc == 0x2026) || (wc == 0x3002) ||
        (wc == 0x3001));
}

IC bool IsBadStartCharacter(xr_wide_char wc)
{
    return (IsNeedSpaceCharacter(wc) || (wc == 0x0021) || (wc == 0x002C) ||
        // ( wc == 0x002D ) ||
        (wc == 0x002E) || (wc == 0x003A) || (wc == 0x003B) || (wc == 0x003F) || (wc == 0x0029) || (wc == 0xFF09));
}

IC bool IsBadEndCharacter(xr_wide_char wc) { return ((wc == 0x0028) || (wc == 0xFF08) || (wc == 0x4E00)); }
IC bool IsAlphaCharacter(xr_wide_char wc)
{
    return (((wc >= 0x0030) && (wc <= 0x0039)) || ((wc >= 0x0041) && (wc <= 0x005A)) ||
        ((wc >= 0x0061) && (wc <= 0x007A)) || ((wc >= 0xFF10) && (wc <= 0xFF19)) ||
        ((wc >= 0xFF21) && (wc <= 0xFF3A)) || ((wc >= 0xFF41) && (wc <= 0xFF5A)));
}

XRCORE_API xr_string StringFromUTF8(const char* string, const std::locale& locale);
XRCORE_API xr_string StringToUTF8(const char* string, const std::locale& locale);
