#pragma once

#include "xrCommon/xr_smart_pointers.h"

// Engine-side contract for text-input delivery. xrEngine owns this
// interface; concrete implementations live next to whichever input
// transport actually surfaces unicode keystrokes (SDL today, a native
// AppKit interpretKeyEvents:/insertText: pipeline tomorrow). The point
// of the contract is to keep CInput's text-input call sites (console,
// MP chat, save-name dialog) free of SDL_* symbols so that swapping
// backends doesn't ripple into the consumer side.
//
// Design choice — minimal Start/Stop surface (A.6 V4 plan
// "Option 2"). The SDL_TEXTINPUT event consumer in CInput::KeyUpdate
// stays where it is: it lives inside the same SDL_PeepEvents loop as
// SDL_KEYDOWN/SDL_KEYUP and shares the textInputCounter race-guard
// with EnableTextInput/DisableTextInput. Splitting the consumer out
// now (Option 1) would force a second SDL_PeepEvents call, weaken the
// counter-vs-event ordering guarantee, and would not earn its cost
// until a native macOS backend actually ships — at which point the
// loop will need a deeper rewrite anyway (NSEvent → insertText: path
// instead of polling an SDL queue).
class ITextInputBackend
{
public:
    virtual ~ITextInputBackend();

    // Enable / disable platform-level text-input delivery (IME on,
    // composed-keystroke routing on). Called by CInput around its
    // reference-counted EnableTextInput / DisableTextInput so the
    // backend sees pure on/off edges, not nested counters.
    virtual void Start() = 0;
    virtual void Stop() = 0;
};

// SDL-backed implementation. Created once during CInput construction.
// Factory hides the SDL header dependency from this contract header.
xr_unique_ptr<ITextInputBackend> CreateSDLTextInputBackend();
