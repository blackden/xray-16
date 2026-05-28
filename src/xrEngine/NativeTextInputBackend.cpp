#include "stdafx.h"
#pragma hdrstop

#include "NativeTextInputBackend.h"
#include "IInputReceiver.h"
#include "xr_input.h"

// Plain-C++ side of the native macOS text-input backend (A.7.2,
// gitea #165). This TU owns the ITextInputBackend implementation
// surface — Start/Stop forwarding into the .mm via the C-style
// OpenXRay_NativeTextInput_* entries declared in the header.
//
// File split rationale lives in NativeTextInputBackend.h header
// comment. Short version: AppKit's <objc/objc.h> defines BOOL as
// `bool`; Common/PlatformBSD.inl typedefs it to int32_t. Including
// both in one TU is a redefinition error. The .mm side skips PCH
// and avoids stdafx; the engine-side glue (this TU) keeps PCH and
// talks to the .mm through plain C only.

CNativeTextInputBackend::CNativeTextInputBackend() = default;

CNativeTextInputBackend::~CNativeTextInputBackend() = default;

void CNativeTextInputBackend::Start()
{
    OpenXRay_NativeTextInput_Activate();
}

void CNativeTextInputBackend::Stop()
{
    OpenXRay_NativeTextInput_Deactivate();
}

xr_unique_ptr<ITextInputBackend> CreateNativeTextInputBackend()
{
    // xr_custom_deleter<T> is invariant in T; build the base pointer
    // directly from an xr_new'd derived instance so the deleter
    // dispatches the virtual destructor. Same pattern as the SDL
    // backend factory.
    return xr_unique_ptr<ITextInputBackend>(xr_new<CNativeTextInputBackend>());
}

// Called from NativeTextInputBackend.mm (insertText: implementation)
// when NSTextInputContext produces committed UTF-8 text. Dispatches
// into the engine the same way CInput::KeyUpdate's SDL_TEXTINPUT
// branch did pre-A.7.2 — top of cbStack receiver, IR_OnTextInput.
// .mm cannot reach pInput / IInputReceiver directly without dragging
// stdafx (which collides with AppKit on the BOOL typedef), so the
// dispatch is funnelled through this C entry.
extern "C" void OpenXRay_DispatchTextInputUTF8(const char* utf8)
{
    if (utf8 == nullptr || *utf8 == '\0')
        return;
    if (pInput == nullptr)
        return;
    IInputReceiver* receiver = pInput->CurrentIR();
    if (receiver != nullptr)
        receiver->IR_OnTextInput(utf8);
}
