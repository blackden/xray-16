#include "stdafx.h"
#pragma hdrstop

#include "SDLTextInputBackend.h"

#include <SDL.h>

#if defined(XR_PLATFORM_APPLE)
// Inline forward-decl — see SDLTextInputBackend.h XXX block. Header for
// macos_cocoa_shim.h deliberately scopes itself to the A.3 NSEvent
// pipeline; this notify symbol piggybacks on the same TU as the gate.
extern "C" void OpenXRay_NotifyTextInputActive(int active);
#endif

void CSDLTextInputBackend::Start()
{
#if defined(XR_PLATFORM_APPLE)
    // Publish gate flag BEFORE SDL_StartTextInput. SDL_StartTextInput
    // re-enters NSApp run loop via [NSTextInputContext activate]; flag
    // must be visible to the NSEvent monitor before that re-entry.
    OpenXRay_NotifyTextInputActive(1);
#endif
    SDL_StartTextInput();
    SDL_PumpEvents();
    SDL_FlushEvents(SDL_TEXTEDITING, SDL_TEXTINPUT);
}

void CSDLTextInputBackend::Stop()
{
#if defined(XR_PLATFORM_APPLE)
    // Clear gate BEFORE SDL_StopTextInput (asymmetric to Start by
    // design). SDL_StopTextInput re-enters NSApp run loop via
    // [NSTextInputContext deactivate]; if the flag were still true
    // during that re-entry, a real gameplay keyDown would be routed
    // to SDL while SDL is mid-teardown of the TEXTINPUT pipeline ->
    // orphan or dropped key. The composed-keystroke worry is moot:
    // anything already in [NSTextInputContext]'s composition buffer
    // delivers via interpretKeyEvents: synchronously before
    // SDL_StopTextInput returns.
    OpenXRay_NotifyTextInputActive(0);
#endif
    SDL_StopTextInput();
    SDL_PumpEvents();
    SDL_FlushEvents(SDL_TEXTEDITING, SDL_TEXTINPUT);
}

bool CSDLTextInputBackend::IsActive() const
{
    return SDL_IsTextInputActive() == SDL_TRUE;
}

xr_unique_ptr<ITextInputBackend> CreateSDLTextInputBackend()
{
    // Note: xr_unique_ptr uses xr_custom_deleter<T> which is invariant
    // in T, so we cannot implicit-convert xr_unique_ptr<Derived> to
    // xr_unique_ptr<Base>. Build the base-typed pointer directly from
    // an xr_new'd derived instance — xr_custom_deleter<Base> calls
    // xr_delete(Base*) which dispatches the virtual dtor.
    return xr_unique_ptr<ITextInputBackend>(xr_new<CSDLTextInputBackend>());
}
