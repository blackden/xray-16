#pragma once

#include "ITextInputBackend.h"

// SDL2-backed ITextInputBackend. Wraps SDL_StartTextInput /
// SDL_StopTextInput.
//
// Post-T2 (gitea #143): SDL_PumpEvents removed from Start/Stop — the
// engine-owned g_textInputActive flag (#141) is now the source of truth
// for the macOS NSEvent gate, so the pump's only remaining job (forcing
// SDL state visible to the gate before re-entry) is obsolete. The
// SDL_FlushEvent(SDL_TEXTEDITING) call in Stop is retained because
// CInput::KeyUpdate's SDL_PeepEvents range (SDL_KEYDOWN..SDL_KEYMAPCHANGED)
// excludes TEXTEDITING; without the flush, mid-composition IME events
// from the outgoing surface would leak into the next session.
class CSDLTextInputBackend final : public ITextInputBackend
{
public:
    CSDLTextInputBackend() = default;
    ~CSDLTextInputBackend() override = default;

    void Start() override;
    void Stop() override;
};
