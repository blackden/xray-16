#include "stdafx.h"
#pragma hdrstop

#include "SDLTextInputBackend.h"

#include <SDL.h>

#if defined(XR_PLATFORM_APPLE)
#include "Common/PostLogMark.hpp"

// Inline forward-decl — see SDLTextInputBackend.h header comment. The
// macos_cocoa_shim.h header deliberately scopes itself to the A.3
// NSEvent pipeline; this notify symbol piggybacks on the same TU as
// the gate.
extern "C" void OpenXRay_NotifyTextInputActive(int active);
#endif

void CSDLTextInputBackend::Start()
{
#if defined(XR_PLATFORM_APPLE)
    // Publish gate flag BEFORE SDL_StartTextInput. SDL_StartTextInput
    // re-enters NSApp run loop via [NSTextInputContext activate]; flag
    // must be visible to the NSEvent monitor before that re-entry.
    OpenXRay_NotifyTextInputActive(1);

    // XXX [smoke][DIAG6-E]: probe TEXTINPUT count at 3 points around
    // SDL_StartTextInput + SDL_FlushEvents to identify how '`' leaks
    // into console (gitea #162). Park, don't strip — recurring input
    // bug family per feedback_instrumentation_strategy.
    SDL_Event peek[16];
    const int p1_before_start = SDL_PeepEvents(peek, 16, SDL_PEEKEVENT,
        SDL_TEXTINPUT, SDL_TEXTINPUT);
#endif

    SDL_StartTextInput();

#if defined(XR_PLATFORM_APPLE)
    const int p2_after_start = SDL_PeepEvents(peek, 16, SDL_PEEKEVENT,
        SDL_TEXTINPUT, SDL_TEXTINPUT);
#endif

    // Drop the whole TEXTEDITING..TEXTINPUT range so a fresh receiver
    // does not see stale composition or final-text from the previous
    // surface.
    SDL_FlushEvents(SDL_TEXTEDITING, SDL_TEXTINPUT);

#if defined(XR_PLATFORM_APPLE)
    const int p3_after_flush = SDL_PeepEvents(peek, 16, SDL_PEEKEVENT,
        SDL_TEXTINPUT, SDL_TEXTINPUT);
    POSTLOG_MARK_FMT("# DIAG6-E backend.Start TEXTINPUT p1=%d p2=%d p3=%d",
        p1_before_start, p2_after_start, p3_after_flush);
#endif
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
    // Narrower than Start: drop in-flight composition only. Final
    // SDL_TEXTINPUT characters already committed by the user must
    // reach the outgoing receiver, since they represent deliberate
    // typing up to the moment the surface closed.
    SDL_FlushEvent(SDL_TEXTEDITING);
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
