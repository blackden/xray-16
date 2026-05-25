// CocoaFullscreenShim — guard against SDL stomping NSWindow.collectionBehavior.
//
// Problem: SDL2 cocoa backend (SDL_cocoawindow.m) calls
// `[NSWindow setCollectionBehavior:]` from multiple entry points
// (`Cocoa_SetWindowResizable`, internal style/level changes). Some of those
// paths reset the mask to `NSWindowCollectionBehaviorDefault`, dropping the
// `NSWindowCollectionBehaviorFullScreenPrimary` bit we need for native Cocoa
// fullscreen Spaces ([NSWindow toggleFullScreen:]). When the bit is missing
// at the moment `SDL_SetWindowFullscreen(DESKTOP)` runs, SDL falls back to a
// borderless overlay — Cmd-Tab minimises to Dock instead of switching Spaces.
//
// We install a KVO observer on the NSWindow's `collectionBehavior` property
// once, then expose `ForceFullscreenPrimary()` to re-assert the mask at every
// transition. If SDL stomps the mask, KVO fires and we re-set it within the
// same runloop turn, logging the event for diagnosis.
//
// Singleton, idempotent. Lives next to macos_cocoa_shim.mm. See #99.
//
// XXX [ragnar] COCOA-GUARD: Hooks for OnErrorDialog/OnFatalError deliberately
// omitted in v1 — those paths take the window OUT of fullscreen and are
// product polish (error dialog appearance during a Space), not core fix.
// Revisit if errors-during-fullscreen show up in real play.

#pragma once

#if defined(XR_PLATFORM_APPLE) || defined(__APPLE__)

#ifdef __cplusplus
extern "C" {
#endif

// Install the KVO guard on the given NSWindow* (cast to void* on the C++
// side — `info.info.cocoa.window` from SDL_SysWMinfo). Idempotent: subsequent
// calls with the same window are no-ops; calls with a different window
// rebind the observer to the new window.
//
// Pass nullptr to uninstall (window teardown).
void OpenXRay_InstallCocoaFullscreenGuard(void* nsWindow);

// Re-assert NSWindowCollectionBehaviorFullScreenPrimary on the currently
// guarded window. Safe to call before guard install (no-op). Should be
// invoked after every SDL_CreateWindow / SDL_SetWindowFullscreen /
// SDL_SetWindowBordered call on macOS so SDL's internal cascades don't leave
// the mask stomped at the moment user toggles fullscreen.
void OpenXRay_ForceFullscreenPrimary(void);

#ifdef __cplusplus
}
#endif

#endif // XR_PLATFORM_APPLE
