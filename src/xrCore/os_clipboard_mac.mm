// macOS clipboard backend for os_clipboard (A.6.1).
//
// Replaces SDL_{Set,Get,Has}ClipboardText on Apple platforms. Rationale:
//
//   * SDL's macOS clipboard path pumps the AppKit event queue under the
//     covers (NSPasteboard access through SDL_VIDEODEVICE), which can
//     re-enter the NSEvent local-monitor installed by macos_cocoa_shim.
//     A.6's textinput contract requires the engine to own NSPasteboard
//     directly so clipboard reads/writes never traverse SDL's pump.
//
//   * Future-proof for the native-rewrite endgame: when SDL window/input
//     goes away (roadmap phase 2), clipboard must already be SDL-free.
//
// Public C surface — consumed by os_clipboard.cpp and xrEngine/Device_imgui.cpp
// via local extern "C" forward declarations (no header to avoid xrCore->xrEngine
// reverse dependency; symbols live in xrCore where os_clipboard.cpp lives).
//
//   const char* OpenXRay_PasteboardGetString(void)  — UTF-8 string, valid
//       until the next OpenXRay_PasteboardGetString call. Owned by a
//       file-static xr_string; caller MUST NOT free. Returns NULL if the
//       pasteboard has no string representation.
//
//   bool OpenXRay_PasteboardSetString(const char* utf8) — copy `utf8` to the
//       general pasteboard. Returns false on nil/empty input or write failure.
//
//   bool OpenXRay_PasteboardHasString(void) — true iff the general pasteboard
//       currently carries a string-type item.
//
// Threading: NSPasteboard generalPasteboard is documented thread-safe for
// reads and writes (each call acquires the underlying paste-server connection
// internally). All callers in this codebase are on the main thread, so no
// extra synchronisation is needed for the file-static return buffer either.

// NB: NO #include "stdafx.h" / Platform.hpp here. PlatformApple.inl declares
// `typedef int32_t BOOL` for the engine; Foundation's objc.h declares
// `typedef bool BOOL`. Pulling both into the same TU is a hard error
// ("typedef redefinition with different types"). This file talks to the OS
// only — no engine types needed — so we keep it isolated.

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>

#include <string>

namespace
{
// Persistent storage so the returned pointer survives until the next call.
// ImGui's Platform_GetClipboardTextFn contract requires this exact lifetime.
// std::string (not xr_string) to avoid pulling xrCore headers that drag in
// the BOOL typedef conflict described above.
std::string g_lastPasteboardGet;
} // namespace

extern "C" const char *OpenXRay_PasteboardGetString(void)
{
    @autoreleasepool
    {
        NSPasteboard *pb = [NSPasteboard generalPasteboard];
        NSString *s = [pb stringForType:NSPasteboardTypeString];
        if (s == nil)
        {
            g_lastPasteboardGet.clear();
            return nullptr;
        }

        const char *utf8 = [s UTF8String];
        if (utf8 == nullptr)
        {
            g_lastPasteboardGet.clear();
            return nullptr;
        }

        g_lastPasteboardGet.assign(utf8);
        return g_lastPasteboardGet.c_str();
    }
}

extern "C" bool OpenXRay_PasteboardSetString(const char *utf8)
{
    if (utf8 == nullptr)
        return false;

    @autoreleasepool
    {
        NSString *s = [NSString stringWithUTF8String:utf8];
        if (s == nil)
            return false;

        NSPasteboard *pb = [NSPasteboard generalPasteboard];
        [pb clearContents];
        BOOL ok = [pb setString:s forType:NSPasteboardTypeString];
        return ok ? true : false;
    }
}

extern "C" bool OpenXRay_PasteboardHasString(void)
{
    @autoreleasepool
    {
        NSPasteboard *pb = [NSPasteboard generalPasteboard];
        NSArray *classes = @[ [NSString class] ];
        BOOL has = [pb canReadObjectForClasses:classes options:nil];
        return has ? true : false;
    }
}
