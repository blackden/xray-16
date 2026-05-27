#include "stdafx.h"
#pragma hdrstop

#include "SDLTextInputBackend.h"

#include <SDL.h>

void CSDLTextInputBackend::Start()
{
    SDL_StartTextInput();
    SDL_PumpEvents();
    SDL_FlushEvents(SDL_TEXTEDITING, SDL_TEXTINPUT);
}

void CSDLTextInputBackend::Stop()
{
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
