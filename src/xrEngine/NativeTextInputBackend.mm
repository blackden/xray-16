// Native macOS text-input backend — Objective-C++ side (A.7.2, gitea #165).
//
// Replaces SDL_TEXTINPUT on macOS with a direct NSTextInputContext
// drive. See NativeTextInputBackend.h header comment for the full
// rationale and trade-offs.
//
// File-split note: this TU is compiled WITHOUT the engine PCH and
// without unity inclusion (set in CMakeLists). `<AppKit/AppKit.h>`
// drags in `<objc/objc.h>` which `typedef bool BOOL;`, while
// `Common/PlatformBSD.inl` (via stdafx.h → Common/Common.hpp) does
// `typedef int32_t BOOL;`. The two definitions are mutually exclusive.
// All engine-side glue (ITextInputBackend implementation, IR_OnTextInput
// dispatch) sits in the companion `NativeTextInputBackend.cpp` TU
// which talks to this file through plain C entries declared at the
// bottom of the header.
//
// Architecture:
//
//   Singleton NSView subclass (OpenXRayTextInputView) that conforms
//   to <NSTextInputClient>. Acts as the NSTextInputClient for a
//   process-wide NSTextInputContext. On Activate we call
//   `[context activate]` — this makes the context AppKit's current
//   input context, and AppKit's responder chain auto-dispatches
//   keyDown events into interpretKeyEvents: which lands in our
//   insertText:replacementRange:. Committed UTF-8 bytes are forwarded
//   to the engine via OpenXRay_DispatchTextInputUTF8.
//
//   AppKit owns dispatch — the NSEvent local monitor does not call
//   any handleEvent: itself. Iteration history: an earlier driver-style
//   variant called [context handleEvent:] from the monitor while the
//   context was also activated, producing two insertText: per keystroke
//   (parallel ingest via [NSWindow keyDown:] + manual driver). Dropping
//   `[activate]` killed text input entirely (DIAG7-F observed delta=0
//   on a detached context). The current split — activate, no manual
//   handleEvent: — yields exactly one AppKit-owned dispatch per
//   keystroke. See notes/decisions/a7-2-native-text-input.md
//   "Final architecture — AppKit-owned text dispatch".
//
//   Threading: all NSTextInputContext interaction happens on the
//   AppKit main thread (which is also the engine main thread today —
//   SDL_PumpEvents and our local monitor share it). The dispatch
//   bridge into IR_OnTextInput runs on the same thread the SDL
//   backend already used, so no consumer re-races.

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>

#include <atomic>
#include <stddef.h>
#include <stdint.h>
// Includes below stay even with DIAG7-B parked under #if 0 — keeping
// them avoids re-deriving the right header set when the probe is
// flipped back on for a future investigation.
#include <stdio.h>  // DIAG7-B fprintf
#include <string.h> // DIAG7-B strlen

// Engine-owned gate flag setter — symbol lives in macos_cocoa_shim.mm.
// Mirrors the same Notify call the SDL backend used so the NSEvent
// monitor's gate flag transitions in sync with Start/Stop. Declared
// inline to avoid pulling the cocoa-shim header.
extern "C" void OpenXRay_NotifyTextInputActive(int active);

// Dispatch bridge into the engine — symbol lives in
// NativeTextInputBackend.cpp. Hands committed UTF-8 bytes to the top
// IInputReceiver on the engine's cbStack as IR_OnTextInput. .mm side
// cannot call pInput / IInputReceiver directly without dragging
// stdafx (BOOL typedef collision with objc.h).
extern "C" void OpenXRay_DispatchTextInputUTF8(const char* utf8);

// Counter bumped by every successful insertText: invocation. Parked
// next to the DIAG7-B probe in insertText: so a future regression
// (double-dispatch returning, or insertText: stalling) can be
// observed via fprintf + counter delta without re-deriving the
// instrumentation from scratch.
//
// `#if 0` block — the probe is kept as a reading artefact for whoever
// next debugs this path. The full DIAG7 family (B/E/F) was used during
// the A.7.2 iteration 3 root-cause hunt: B = «insertText: fired with
// utf8=...», E = «handler entry, ctx=..., cvar=...», F = «handleEvent:
// delta=...». E/F survive as forensic comments in the architecture
// block at the top of this file; B (the most reusable signal) lives
// here behind a 0/1 switch.
//
// To re-enable: flip the `#if 0` to `#if 1` here, rebuild, ship,
// reproduce the input. Each call to insertText: emits one line to
// stderr; combined with DIAG7-E/F (re-derivable from the architecture
// comment) you get full visibility into «who fired insertText: and
// what did handleEvent: think it did».
#if 0
namespace
{
std::atomic<int> g_insertTextDepth{0};
} // namespace
#endif

@interface OpenXRayTextInputView : NSView <NSTextInputClient>
@end

@implementation OpenXRayTextInputView

// ---------------------------------------------------------------------
// NSTextInputClient — the only callback that does real work.
//
// AppKit calls this from interpretKeyEvents: (auto-dispatched when
// our activated NSTextInputContext is the process-wide current input
// context) once a keystroke has produced committed text. For ASCII /
// Cyrillic precomposed input this fires synchronously inside AppKit's
// keyDown handling; for dead-key compositions it fires when the user
// completes the sequence.
//
// `insertString` is either NSString (plain) or NSAttributedString
// (with attribute runs we don't care about). Strip attributes via
// -string when needed.
// ---------------------------------------------------------------------
- (void)insertText:(id)insertString replacementRange:(NSRange)replacementRange
{
    (void)replacementRange;

    NSString* text = nil;
    if ([insertString isKindOfClass:[NSAttributedString class]])
        text = [(NSAttributedString*)insertString string];
    else if ([insertString isKindOfClass:[NSString class]])
        text = (NSString*)insertString;

    if (text.length == 0)
        return;

    const char* utf8 = [text UTF8String];
    if (utf8 == NULL)
        return;

    // DIAG7-B probe — historical instrumentation that confirmed
    // iteration-3 fixed the double-dispatch (40 fires for 40
    // keystrokes during the 2026-05-28 smoke). Kept as a #if 0 block
    // so future investigators of this path see the «right probe» to
    // re-enable rather than re-deriving it from scratch. See the
    // explanatory block next to g_insertTextDepth declaration above.
#if 0
    fprintf(stderr, "==> DIAG7-B insertText utf8=[%s] len=%zu\n",
            utf8 ? utf8 : "<null>", utf8 ? strlen(utf8) : 0u);
    fflush(stderr);
#endif

    OpenXRay_DispatchTextInputUTF8(utf8);

#if 0
    // Counter parked alongside DIAG7-B probe — see declaration. Not
    // read by any production code; available for future regression
    // diagnosis.
    g_insertTextDepth.fetch_add(1, std::memory_order_release);
#endif
}

// ---------------------------------------------------------------------
// NSTextInputClient — composition / marked-text protocol. We do not
// surface IME composition preview (see header trade-off note), so
// every marked-text query is a polite no-op / sentinel.
// ---------------------------------------------------------------------
- (void)setMarkedText:(id)string
        selectedRange:(NSRange)selectedRange
     replacementRange:(NSRange)replacementRange
{
    (void)string;
    (void)selectedRange;
    (void)replacementRange;
}

- (void)unmarkText
{
}

- (NSRange)selectedRange
{
    return NSMakeRange(NSNotFound, 0);
}

- (NSRange)markedRange
{
    return NSMakeRange(NSNotFound, 0);
}

- (BOOL)hasMarkedText
{
    return NO;
}

- (NSAttributedString*)attributedSubstringForProposedRange:(NSRange)range
                                               actualRange:(NSRangePointer)actualRange
{
    (void)range;
    (void)actualRange;
    return nil;
}

- (NSArray<NSAttributedStringKey>*)validAttributesForMarkedText
{
    return @[];
}

- (NSRect)firstRectForCharacterRange:(NSRange)range actualRange:(NSRangePointer)actualRange
{
    (void)range;
    (void)actualRange;
    return NSZeroRect;
}

- (NSUInteger)characterIndexForPoint:(NSPoint)point
{
    (void)point;
    return NSNotFound;
}

// Unhandled selectors (insertNewline:, deleteBackward:, moveLeft:,
// etc.) — drop. The engine receives these as raw IR_OnKeyboardPress
// events via the A.3 ring fallback in macos_cocoa_shim.mm, so
// duplicating them here would double-dispatch.
- (void)doCommandBySelector:(SEL)selector
{
    (void)selector;
}

@end

// File-static singleton NSTextInputContext — created lazily on first
// activate. One global context is fine: only one text-input surface
// is ever active at a time in the engine (CInput::textInputCounter
// asserts a single edge of activation).
static OpenXRayTextInputView* sTextInputView    = nil;
static NSTextInputContext*    sTextInputContext = nil;

namespace
{
// Lazy-init: NSTextInputContext requires AppKit to be running and
// NSApplication's run loop to exist. Construction at first activate
// is safe — the SDL bootstrap (which initialises NSApp) runs before
// any text-input surface opens.
void EnsureContextInitialized()
{
    if (sTextInputContext != nil)
        return;

    sTextInputView    = [[OpenXRayTextInputView alloc] initWithFrame:NSZeroRect];
    sTextInputContext = [[NSTextInputContext alloc] initWithClient:sTextInputView];
}
} // namespace

extern "C" void OpenXRay_NativeTextInput_Activate(void)
{
    @autoreleasepool
    {
        EnsureContextInitialized();

        // Publish gate flag — NSEvent local monitor reads
        // g_textInputActive to decide whether to consume KeyDown for
        // text input or route into the A.3 ring.
        OpenXRay_NotifyTextInputActive(1);

        // Activate makes the context process-wide current. AppKit
        // then auto-dispatches keyDown events to insertText: on our
        // client view via the responder chain — that single path
        // (AppKit-owned) is the only producer of insertText: now.
        // See OpenXRay_HandleNativeTextInputKeyDown below: the
        // NSEvent local monitor does NOT manually call
        // [context handleEvent:] anymore; doing so on top of AppKit's
        // own dispatch double-fires insertText:. A detached context
        // (no [activate]) doesn't dispatch at all (DIAG7-F observed
        // delta=0), so activate is required.
        [sTextInputContext activate];
    }
}

extern "C" void OpenXRay_NativeTextInput_Deactivate(void)
{
    @autoreleasepool
    {
        // Symmetric — drop process-wide current-context status so
        // AppKit stops dispatching keyDown to our insertText: when
        // the engine flips out of text-input mode.
        if (sTextInputContext != nil)
            [sTextInputContext deactivate];

        OpenXRay_NotifyTextInputActive(0);
    }
}

// ---------------------------------------------------------------------
// C entry — called from the NSEvent local monitor in
// macos_cocoa_shim.mm when KeyDown arrives while g_textInputActive
// is set.
//
// AppKit's activated NSTextInputContext auto-dispatches keyDown to
// insertText: on our client view via the responder chain. The local
// monitor calls this entry only to ask «should I push the event into
// the A.3 ring as well?». Return value is the gate: 0 → monitor
// pushes scancode into A.3 ring (needed so ESC / arrows / Enter still
// reach IR_OnKeyboardPress for nav inside the console / ImGui
// surface); 1 would suppress A.3 entirely.
//
// We always return 0. AppKit fires insertText: for printable keys
// (handled by the consumer via IR_OnTextInput); for control keys
// AppKit calls doCommandBySelector: on our view stub (no-op) so
// insertText: never fires for ESC / arrows / Enter, leaving the A.3
// ring path as the sole receiver of those events. No double-dispatch
// because the two paths cover disjoint key classes — printable goes
// to insertText: only, control goes to A.3 ring only.
// ---------------------------------------------------------------------
extern "C" int OpenXRay_HandleNativeTextInputKeyDown(void* nsevent)
{
    (void)nsevent;
    return 0;
}
