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

void os_clipboard::copy_to_clipboard(pcstr buf, bool alreadyUTF8 /*= false*/)
{
    int result;
    // Post-Phase 1 the engine works in UTF-8 internally; valid UTF-8 input
    // gets passed straight to SDL. Only fall back to the lossy narrow->wide
    // bridge for the rare legacy callers that still ship cp1251 byte
    // strings -- and even then, the in-place transcode preserves cyrillic
    // instead of dropping it like the previous C-locale path did.
    if (alreadyUTF8 || xr_is_valid_utf8(buf))
    {
        result = SDL_SetClipboardText(buf);
    }
    else
    {
        char tmp[1024];
        xr_strcpy(tmp, sizeof(tmp), buf);
        xr_cp1251_to_utf8(tmp, sizeof(tmp));
        result = SDL_SetClipboardText(tmp);
    }
    if (result < 0)
    {
        Msg("! Failed to copy text to the clipboard: %s", SDL_GetError());
        Log(buf);
    }
}

void os_clipboard::paste_from_clipboard(pstr buffer, size_t buffer_size)
{
    VERIFY(buffer);
    VERIFY(buffer_size > 0);

    if (!SDL_HasClipboardText())
        return;

    char* clipData = SDL_GetClipboardText();

    if (!clipData)
    {
        Msg("! Failed to paste text from the clipboard: %s", SDL_GetError());
        return;
    }

    // SDL guarantees UTF-8 from the system clipboard; copy bytes verbatim
    // and let the codepoint-aware renderer handle them. The previous
    // narrow-locale conversion turned cyrillic clipboard content into '?'.
    strncpy_s(buffer, buffer_size, clipData, buffer_size - 1);
    SDL_free(clipData);

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

    if (!SDL_HasClipboardText())
    {
        copy_to_clipboard(string);
        return;
    }

    char* clipData = SDL_GetClipboardText();

    if (!clipData)
    {
        VERIFY3(clipData, "Failed to get text from the clipboard", SDL_GetError());
        copy_to_clipboard(string);
        return;
    }

    // Both sides are UTF-8: SDL clipboard and the input string (now that
    // the engine works in UTF-8). Concatenate raw bytes.
    const size_t clipLength = xr_strlen(clipData);
    const size_t stringLength = xr_strlen(string);
    const size_t bufferSize = (clipLength + stringLength + 1) * sizeof(char);

    pstr buffer = (pstr)xr_alloca(bufferSize);
    xr_strcpy(buffer, bufferSize, clipData);
    xr_strcat(buffer, bufferSize, string);

    SDL_free(clipData);

    copy_to_clipboard(buffer, true);
}
