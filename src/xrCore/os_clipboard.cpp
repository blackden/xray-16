////////////////////////////////////////////////////////////////////////////
// Module : os_clipboard.cpp
// Created : 21.02.2008
// Author : Evgeniy Sokolov
// Description : os clipboard class implementation
//
// Modified : 24.07.2018
// Modified by : Xottab_DUTY
////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"

#include "os_clipboard.h"
#include "Text/StringConversion.hpp"

#include <SDL.h>

#include <locale>

#if defined(XR_PLATFORM_APPLE)
// A.6.1 — clipboard backed by NSPasteboard via os_clipboard_mac.mm so the
// engine no longer pumps the AppKit event loop through SDL_GetClipboardText.
// Symbols live in xrCore (same library); declared here to avoid leaking
// AppKit headers into this TU.
extern "C" const char *OpenXRay_PasteboardGetString(void);
extern "C" bool        OpenXRay_PasteboardSetString(const char *utf8);
extern "C" bool        OpenXRay_PasteboardHasString(void);
#endif

void os_clipboard::copy_to_clipboard(pcstr buf, bool alreadyUTF8 /*= false*/)
{
    // Post-Phase 1 the engine works in UTF-8 internally; valid UTF-8 input
    // gets passed straight to the OS clipboard. Only fall back to the lossy
    // narrow->wide bridge for the rare legacy callers that still ship cp1251
    // byte strings -- and even then, the in-place transcode preserves
    // cyrillic instead of dropping it like the previous C-locale path did.
    pcstr payload = buf;
    char tmp[1024];
    if (!alreadyUTF8 && !xr_is_valid_utf8(buf))
    {
        xr_strcpy(tmp, sizeof(tmp), buf);
        xr_cp1251_to_utf8(tmp, sizeof(tmp));
        payload = tmp;
    }

#if defined(XR_PLATFORM_APPLE)
    if (!OpenXRay_PasteboardSetString(payload))
    {
        Msg("! Failed to copy text to the clipboard (NSPasteboard)");
        Log(buf);
    }
#else
    if (SDL_SetClipboardText(payload) < 0)
    {
        Msg("! Failed to copy text to the clipboard: %s", SDL_GetError());
        Log(buf);
    }
#endif
}

void os_clipboard::paste_from_clipboard(pstr buffer, size_t buffer_size)
{
    VERIFY(buffer);
    VERIFY(buffer_size > 0);

#if defined(XR_PLATFORM_APPLE)
    if (!OpenXRay_PasteboardHasString())
        return;

    const char *clipData = OpenXRay_PasteboardGetString();
    if (!clipData)
    {
        Msg("! Failed to paste text from the clipboard (NSPasteboard)");
        return;
    }
#else
    if (!SDL_HasClipboardText())
        return;

    char *clipData = SDL_GetClipboardText();
    if (!clipData)
    {
        Msg("! Failed to paste text from the clipboard: %s", SDL_GetError());
        return;
    }
#endif

    // UTF-8 guaranteed by both backends (NSPasteboardTypeString is UTF-8,
    // SDL2 normalises the system clipboard to UTF-8); copy bytes verbatim
    // and let the codepoint-aware renderer handle them. The previous
    // narrow-locale conversion turned cyrillic clipboard content into '?'.
    strncpy_s(buffer, buffer_size, clipData, buffer_size - 1);
#if !defined(XR_PLATFORM_APPLE)
    SDL_free(clipData);
#endif

    // Sanitize control bytes (tab/newline) so a multi-line clipboard paste
    // doesn't break the single-line edit. We sweep on the raw byte stream;
    // for UTF-8 continuation bytes (>= 0x80) we skip the check, since the
    // ctype check is only meaningful for ASCII.
    const size_t length = xr_strlen(buffer);
    for (size_t i = 0; i < length; ++i)
    {
        const unsigned char c = static_cast<unsigned char>(buffer[i]);
        if (c >= 0x80) continue; // UTF-8 multibyte — leave alone
        if (c == '\t' || c == '\n' || (c < 0x20 && c != 0))
            buffer[i] = ' ';
    }
}

void os_clipboard::update_clipboard(pcstr string)
{
    if (!string)
    {
        Log("! Why are you trying to copy nullptr to the clipboard?!");
        return;
    }

#if defined(XR_PLATFORM_APPLE)
    if (!OpenXRay_PasteboardHasString())
    {
        copy_to_clipboard(string);
        return;
    }

    const char *clipData = OpenXRay_PasteboardGetString();
    if (!clipData)
    {
        VERIFY3(clipData, "Failed to get text from the clipboard", "NSPasteboard");
        copy_to_clipboard(string);
        return;
    }
#else
    if (!SDL_HasClipboardText())
    {
        copy_to_clipboard(string);
        return;
    }

    char *clipData = SDL_GetClipboardText();
    if (!clipData)
    {
        VERIFY3(clipData, "Failed to get text from the clipboard", SDL_GetError());
        copy_to_clipboard(string);
        return;
    }
#endif

    // Both sides are UTF-8: OS clipboard and the input string (now that
    // the engine works in UTF-8). Concatenate raw bytes.
    const size_t clipLength = xr_strlen(clipData);
    const size_t stringLength = xr_strlen(string);
    const size_t bufferSize = (clipLength + stringLength + 1) * sizeof(char);

    pstr buffer = (pstr)xr_alloca(bufferSize);
    xr_strcpy(buffer, bufferSize, clipData);
    xr_strcat(buffer, bufferSize, string);

#if !defined(XR_PLATFORM_APPLE)
    SDL_free(clipData);
#endif

    copy_to_clipboard(buffer, true);
}
