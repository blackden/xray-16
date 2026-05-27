#pragma once

#include "ITextInputBackend.h"

// SDL2-backed ITextInputBackend. Wraps SDL_StartTextInput /
// SDL_StopTextInput / SDL_IsTextInputActive. The post-toggle
// SDL_PumpEvents + SDL_FlushEvents(SDL_TEXTEDITING, SDL_TEXTINPUT)
// pair is preserved here verbatim from the pre-refactor xr_input.cpp
// implementation:
//
//   * It flushes any stale composed keystrokes left in the SDL queue
//     from a previous focus surface so the new receiver doesn't see
//     leakage.
//   * It pumps so that SDL's internal "text input active" flag is
//     synchronised before the call returns. The NSEvent local
//     monitor in macos_cocoa_shim.mm gates its keyDown swallow on
//     SDL_IsTextInputActive(); without the pump the gate races with
//     the toggle for one frame (gitea #124 follow-up territory).
//
// XXX [ragnar] A6-PUMP-REMOVAL: the cpp-engineer consilium
// (2026-05-27) called the pump pair "hygiene, not load-bearing".
// Dropping it requires either (a) deferring Start/Stop to the next
// frame boundary so the NSEvent monitor never observes a mid-frame
// flip, or (b) maintaining a private g_textInputActive flag in the
// shim that's toggled around CInput::Enable/DisableTextInput
// directly. Out of A.6.3 scope — revisit when the native macOS
// text-input backend lands and the shim's flag is the source of
// truth anyway.
class CSDLTextInputBackend final : public ITextInputBackend
{
public:
    CSDLTextInputBackend() = default;
    ~CSDLTextInputBackend() override = default;

    void Start() override;
    void Stop() override;
    bool IsActive() const override;
};
