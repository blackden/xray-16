#pragma once

#include "ITextInputBackend.h"

// Native macOS text-input backend (A.7.2, gitea #165).
//
// Replaces the SDL_StartTextInput/SDL_StopTextInput pathway with a
// direct AppKit NSTextInputContext owned by the engine. Committed
// UTF-8 keystrokes arrive via NSTextInputClient's insertText: callback
// and are dispatched into IR_OnTextInput on the top callback receiver
// — exactly the same consumer signature the SDL backend fed before.
//
// Why this exists: SDL_TEXTINPUT was the last remaining reason the
// NSEvent local monitor had to selectively let keyDown events through
// to SDL (the "text-input gate"). That pass-through created a parallel
// ingest race ([NSWindow keyDown:] vs A.3 ring) that took 4+ PRs of
// double-dispatch / phantom-key whack-a-mole to mitigate (#155, #156,
// #157, #158, #159, #160, #162). Owning the input context ourselves
// makes the NSEvent monitor authoritative for every keystroke
// regardless of mode: gameplay → A.3 ring → IR_OnKeyboard*, text
// surfaces → [context handleEvent:] → insertText: → IR_OnTextInput.
//
// Trade-off (accepted, see notes/decisions/a7-2-native-text-input.md):
// committed UTF-8 only. No visible IME composition preview ("marked
// text" feedback rendering), no dead-key composition indicator. Latin
// + Cyrillic precomposed input + Emoji-picker commit work; dead-keys
// commit through interpretKeyEvents:'s internal state but the user
// has no visual feedback during composition. Acceptable for CoP —
// console / save-name / MP-chat surfaces all want committed text,
// and none of them currently render marked text.
//
// File split (build-system note): the AppKit-touching code lives in
// `NativeTextInputBackend.mm` and is compiled WITHOUT the engine PCH
// (stdafx.h) — `<AppKit/AppKit.h>` collides with `Common/PlatformBSD.inl`
// over `BOOL` (objc.h: typedef bool; PlatformBSD.inl: typedef int32_t).
// The factory + backend class implementation live here as a plain .cpp
// `NativeTextInputBackend.cpp` companion that DOES include stdafx and
// forwards into the .mm through plain C entries.
class CNativeTextInputBackend final : public ITextInputBackend
{
public:
    CNativeTextInputBackend();
    ~CNativeTextInputBackend() override;

    void Start() override;
    void Stop() override;
};

// Factory. Returns a base-typed unique_ptr (the xr_custom_deleter is
// invariant in T, so we cannot implicit-convert derived → base).
xr_unique_ptr<ITextInputBackend> CreateNativeTextInputBackend();

// C entries implemented in NativeTextInputBackend.mm. Plain C surface
// is the only thing the .cpp side can call without dragging
// `<AppKit/AppKit.h>` into a PCH-using TU.
#ifdef __cplusplus
extern "C"
{
#endif

// Activate / deactivate our owned NSTextInputContext. Called from
// CNativeTextInputBackend::Start/Stop.
void OpenXRay_NativeTextInput_Activate(void);
void OpenXRay_NativeTextInput_Deactivate(void);

// Called by the NSEvent local monitor (macos_cocoa_shim.mm) when a
// KeyDown arrives in text-input mode. Hands the event to our
// NSTextInputContext via [context handleEvent:]; insertText: fires
// synchronously for committed characters. Returns 1 if committed
// text was produced this call, 0 if not — caller falls back to the
// A.3 ring path for non-text keys (Enter, Esc, arrows, function
// keys) so navigation in text surfaces still works.
//
// nsevent is type-erased (void*) on purpose — this header is
// consumed by plain C++ TUs that cannot pull in NSEvent.
int OpenXRay_HandleNativeTextInputKeyDown(void* nsevent);

#ifdef __cplusplus
}
#endif
