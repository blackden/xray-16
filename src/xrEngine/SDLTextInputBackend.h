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
// XXX [ragnar] A6-PUMP-REMOVAL: T1 (gitea #141) replaced the
// SDL_IsTextInputActive() gate in macos_cocoa_shim.mm with an
// engine-owned std::atomic<bool> toggled directly by Start/Stop
// here via OpenXRay_NotifyTextInputActive — the re-entry race that
// motivated the pump pair is gone, so dropping SDL_PumpEvents from
// both Start and Stop is the next step (PR T2). Keep
// SDL_FlushEvent(SDL_TEXTEDITING) in Stop when T2 lands: KeyUpdate's
// SDL_PeepEvents range is SDL_KEYDOWN..SDL_KEYMAPCHANGED and does
// NOT cover TEXTEDITING, so half-composed IME events would leak
// between sessions without an explicit flush.
class CSDLTextInputBackend final : public ITextInputBackend
{
public:
    CSDLTextInputBackend() = default;
    ~CSDLTextInputBackend() override = default;

    void Start() override;
    void Stop() override;
    bool IsActive() const override;
};
