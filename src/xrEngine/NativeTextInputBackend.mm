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
//   to <NSTextInputClient>. Never installed in any window — we drive
//   the input pipeline manually by feeding NSEvents into our own
//   NSTextInputContext via [context handleEvent:]. NSTextInputContext
//   then invokes interpretKeyEvents:-style dispatch which lands in
//   our insertText:replacementRange: implementation, where we forward
//   committed UTF-8 bytes to the engine via
//   OpenXRay_DispatchTextInputUTF8.
//
//   The view is detached (no superview / no firstResponder) because
//   our A.3 NSEvent local monitor swallows keyDown before it can
//   reach the responder chain (see
//   notes/decisions/a6-textinput-contract.md "NSEvent local monitor
//   returning nil ... blocks responder chain полностью"). Detached
//   is fine: NSTextInputContext does not require the client view to
//   be hosted in a window in order to translate keystrokes via
//   [context handleEvent:] — it only needs the client to respond to
//   the NSTextInputClient protocol.
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
#include <stdio.h>  // XXX [smoke][DIAG7-B] fprintf
#include <string.h> // XXX [smoke][DIAG7-B] strlen

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

// Counter bumped by every successful insertText: invocation. Lets
// OpenXRay_HandleNativeTextInputKeyDown tell whether
// [context handleEvent:] produced committed text in this call
// (counter delta > 0) or was absorbed mid-composition / no-op
// (delta == 0). NSTextInputContext's BOOL return from handleEvent:
// is over-broad (YES for any observed key even when no text was
// committed), so we measure the only signal that matters to the
// engine: did the bridge fire.
namespace
{
std::atomic<int> g_insertTextDepth{0};
} // namespace

@interface OpenXRayTextInputView : NSView <NSTextInputClient>
@end

@implementation OpenXRayTextInputView

// ---------------------------------------------------------------------
// NSTextInputClient — the only callback that does real work.
//
// AppKit calls this when interpretKeyEvents: (driven via
// [sTextInputContext handleEvent:]) decides a keystroke has produced
// committed text. For ASCII / Cyrillic precomposed input this fires
// synchronously inside [context handleEvent:]; for dead-key
// compositions it fires when the user completes the sequence.
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

    // XXX [smoke][DIAG7-B]: A.7.2 reports double-dispatch — keystroke
    // appearing twice in console / ImGui inputs. Park until source
    // root-caused (likely [NSWindow keyDown:] parallel ingest reaching
    // a second handleEvent path). Strip in A.7.4+ when NSWindow
    // ownership consolidates the responder chain.
    fprintf(stderr, "==> DIAG7-B insertText utf8=[%s] len=%zu\n",
            utf8 ? utf8 : "<null>", utf8 ? strlen(utf8) : 0u);
    fflush(stderr);

    OpenXRay_DispatchTextInputUTF8(utf8);

    // Signal to OpenXRay_HandleNativeTextInputKeyDown below that this
    // [context handleEvent:] produced committed text. Release pairs
    // with acquire on the read side.
    g_insertTextDepth.fetch_add(1, std::memory_order_release);
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
        // g_textInputActive to decide whether to route KeyDown into
        // the native text-input path ([context handleEvent:]) or the
        // A.3 ring.
        //
        // NOTE: must NOT call [sTextInputContext activate]. Activate
        // installs the context process-wide as the current input
        // context, after which AppKit auto-routes every keyDown that
        // reaches the responder chain (via [NSWindow keyDown:]
        // parallel ingest path that our local NSEvent monitor cannot
        // intercept) to our singleton view's insertText:. Combined
        // with our manual [context handleEvent:] driver from the
        // local monitor this fires insertText: TWICE per keystroke
        // and doubles every character in the ImGui InputText field.
        // handleEvent: works on detached/un-activated contexts —
        // driver-style usage stays here.
        OpenXRay_NotifyTextInputActive(1);
    }
}

extern "C" void OpenXRay_NativeTextInput_Deactivate(void)
{
    @autoreleasepool
    {
        // Symmetric — see Activate. No deactivate either: context was
        // never activated, so nothing to undo besides the gate flag.
        OpenXRay_NotifyTextInputActive(0);
    }
}

// ---------------------------------------------------------------------
// C entry — called from the NSEvent local monitor in
// macos_cocoa_shim.mm when KeyDown arrives while g_textInputActive
// is set. Hands the event to NSTextInputContext via handleEvent:; the
// context invokes insertText: synchronously for committed characters.
//
// Returns 1 if the event produced committed text (insertText: fired
// during this call), 0 otherwise. The caller (cocoa shim) uses the
// return to decide whether to fall back to the A.3 ring path so
// non-printable keys (Enter, Escape, Backspace, arrows) still reach
// gameplay receivers via IR_OnKeyboardPress.
// ---------------------------------------------------------------------
extern "C" int OpenXRay_HandleNativeTextInputKeyDown(void* nsevent)
{
    if (nsevent == NULL || sTextInputContext == nil)
        return 0;

    @autoreleasepool
    {
        const int before = g_insertTextDepth.load(std::memory_order_acquire);
        NSEvent*  event  = (__bridge NSEvent*)nsevent;
        [sTextInputContext handleEvent:event];
        const int after = g_insertTextDepth.load(std::memory_order_acquire);
        return (after > before) ? 1 : 0;
    }
}
