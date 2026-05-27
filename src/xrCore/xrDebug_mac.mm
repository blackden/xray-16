// macOS NSAlert backend for xrDebug message boxes (A.6.2).
//
// Replaces SDL_ShowSimpleMessageBox / SDL_ShowMessageBox on Apple platforms.
// Rationale mirrors A.6.1 (NSPasteboard clipboard): SDL's message-box path
// on macOS internally drives AppKit modal sessions through its own window
// subsystem, which can re-enter SDL's event pump and interact badly with
// the NSEvent local-monitor installed by macos_cocoa_shim. The engine owns
// fatal-dialog presentation directly so assertion / fatal paths never go
// through SDL on macOS. Also future-proofs for the native-rewrite endgame
// (roadmap phase 2) when SDL window/input goes away.
//
// Public C surface — consumed by xrDebug.cpp via local extern "C" forward
// declarations (no header to avoid leaking AppKit types into engine
// translation units; pattern established by os_clipboard_mac.mm in A.6.1).
//
//   int OpenXRay_ShowSimpleAlert(int flags, const char* title, const char* message)
//       Simple modal "OK" alert. Returns 0 on success, non-zero on failure
//       (NSAlert init failure, nil arguments). `flags` maps SDL_MessageBox
//       flags (0=info, 1=warning, 2=error) onto NSAlertStyle.
//
//   int OpenXRay_ShowAlertWithButtons(int flags, const char* title,
//                                     const char* message,
//                                     const char* const* buttons,
//                                     int* outPressedIndex)
//       Modal alert with multiple buttons. `buttons` is a NULL-terminated
//       array of UTF-8 labels added left-to-right; NSAlert convention is
//       that the FIRST added button is the default (rightmost in the
//       sheet) and binds to Return — we add them in caller order so the
//       caller controls that. `*outPressedIndex` receives the zero-based
//       index into `buttons[]` of the clicked button. Returns 0 on
//       success, non-zero on failure.
//
// Threading: NSAlert is documented main-thread-only. xrDebug fatal paths
// can fire from any thread (assertion in render thread, sound thread,
// task workers). We dispatch to main via dispatch_sync when invoked off
// main. If the main thread is itself wedged (e.g. xr_3da hang during
// teardown) dispatch_sync will deadlock — at that point the user uses
// macOS Force Quit, which matches the documented landmine for this fork
// ("never kill xr_3da — ask ragnar"; OS-level fallback is the contract).
// We deliberately do NOT silently bypass the dispatch on deadlock: a
// missing fatal dialog is worse than a hang the user can resolve via
// Force Quit, because the missing dialog hides the assertion text.

// NB: NO #include "stdafx.h" / Platform.hpp here. PlatformApple.inl declares
// `typedef int32_t BOOL` for the engine; Foundation's objc.h declares
// `typedef bool BOOL`. Pulling both into the same TU is a hard error
// ("typedef redefinition with different types"). This file talks to the OS
// only — no engine types needed — so we keep it isolated. (Same pattern
// as os_clipboard_mac.mm from A.6.1.)

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>

#include <dispatch/dispatch.h>

// SDL_MessageBox flag values inlined to avoid pulling SDL into this TU
// (and the BOOL conflict it would drag in via SDL_stdinc.h). These match
// SDL2's SDL_messagebox.h definitions; the mapping is one-way (caller
// passes the same int it would pass to SDL_ShowMessageBox).
//
//   SDL_MESSAGEBOX_ERROR       = 0x00000010
//   SDL_MESSAGEBOX_WARNING     = 0x00000020
//   SDL_MESSAGEBOX_INFORMATION = 0x00000040
//
// We accept any bitmask and pick the highest-severity bit set. If none of
// the three bits is set we default to NSAlertStyleInformational.
static NSAlertStyle MapSDLFlagsToNSAlertStyle(int flags)
{
    if (flags & 0x00000010) // SDL_MESSAGEBOX_ERROR
        return NSAlertStyleCritical;
    if (flags & 0x00000020) // SDL_MESSAGEBOX_WARNING
        return NSAlertStyleWarning;
    return NSAlertStyleInformational;
}

// Build and run a simple OK alert on the current (main) thread.
// Caller MUST be on the main thread when this is invoked.
static int RunSimpleAlertMainThread(int flags, const char *title, const char *message)
{
    @autoreleasepool
    {
        NSAlert *alert = [[NSAlert alloc] init];
        if (alert == nil)
            return -1;

        alert.alertStyle = MapSDLFlagsToNSAlertStyle(flags);

        if (title != nullptr)
        {
            NSString *t = [NSString stringWithUTF8String:title];
            if (t != nil)
                alert.messageText = t;
        }
        if (message != nullptr)
        {
            NSString *m = [NSString stringWithUTF8String:message];
            if (m != nil)
                alert.informativeText = m;
        }

        // Single default OK button. NSAlert adds one implicitly if none are
        // added, but being explicit avoids depending on that behaviour.
        [alert addButtonWithTitle:@"OK"];

        (void)[alert runModal];
        return 0;
    }
}

// Build and run a multi-button alert on the current (main) thread.
// Caller MUST be on the main thread when this is invoked.
static int RunAlertWithButtonsMainThread(int flags, const char *title, const char *message,
    const char *const *buttons, int *outPressedIndex)
{
    if (outPressedIndex == nullptr)
        return -1;
    *outPressedIndex = -1;

    @autoreleasepool
    {
        NSAlert *alert = [[NSAlert alloc] init];
        if (alert == nil)
            return -1;

        alert.alertStyle = MapSDLFlagsToNSAlertStyle(flags);

        if (title != nullptr)
        {
            NSString *t = [NSString stringWithUTF8String:title];
            if (t != nil)
                alert.messageText = t;
        }
        if (message != nullptr)
        {
            NSString *m = [NSString stringWithUTF8String:message];
            if (m != nil)
                alert.informativeText = m;
        }

        int count = 0;
        if (buttons != nullptr)
        {
            for (int i = 0; buttons[i] != nullptr; ++i)
            {
                NSString *label = [NSString stringWithUTF8String:buttons[i]];
                if (label == nil)
                    label = @"";
                [alert addButtonWithTitle:label];
                ++count;
            }
        }
        if (count == 0)
        {
            // Fall back to a single OK so the alert is still dismissible.
            [alert addButtonWithTitle:@"OK"];
            count = 1;
        }

        NSModalResponse resp = [alert runModal];

        // NSAlert returns NSAlertFirstButtonReturn (1000) for the first
        // added button, NSAlertSecondButtonReturn for the second, etc.
        // Map back to a zero-based index. Clamp to the count we added
        // in case AppKit ever surprises us with an out-of-range value.
        long idx = (long)resp - (long)NSAlertFirstButtonReturn;
        if (idx < 0 || idx >= count)
            idx = 0;
        *outPressedIndex = (int)idx;
        return 0;
    }
}

extern "C" int OpenXRay_ShowSimpleAlert(int flags, const char *title, const char *message)
{
    if ([NSThread isMainThread])
        return RunSimpleAlertMainThread(flags, title, message);

    __block int rc = -1;
    // dispatch_sync to main; if main is wedged this deadlocks (documented
    // tradeoff above). We accept that — losing the assertion dialog would
    // be worse than the hang, which the user resolves via macOS Force Quit.
    dispatch_sync(dispatch_get_main_queue(), ^{
        rc = RunSimpleAlertMainThread(flags, title, message);
    });
    return rc;
}

extern "C" int OpenXRay_ShowAlertWithButtons(int flags, const char *title, const char *message,
    const char *const *buttons, int *outPressedIndex)
{
    if ([NSThread isMainThread])
        return RunAlertWithButtonsMainThread(flags, title, message, buttons, outPressedIndex);

    __block int rc = -1;
    dispatch_sync(dispatch_get_main_queue(), ^{
        rc = RunAlertWithButtonsMainThread(flags, title, message, buttons, outPressedIndex);
    });
    return rc;
}
