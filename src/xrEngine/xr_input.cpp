#include "stdafx.h"
#pragma hdrstop

#include "xr_input.h"
#include "ITextInputBackend.h"
#if defined(XR_PLATFORM_APPLE)
#include "NativeTextInputBackend.h"
#endif
#include "IInputReceiver.h"
#include "GameFont.h"
#include "XR_IOConsole.h"
#include "xrCore/Text/StringConversion.hpp"

#include <locale>

#if defined(XR_PLATFORM_APPLE)
// Apple HID virtual key codes (from <Carbon/HIToolbox/Events.h>).
// Inline-defined here because <Carbon/HIToolbox/Events.h> only resolves
// in Objective-C/C++ TUs (.m/.mm) — plain .cpp builds reject the
// sub-framework include syntax even though the header exists. These
// values are frozen since the Apple Extended Keyboard II (~1990) and
// will not change.
namespace
{
constexpr int kVK_ANSI_A              = 0x00;
constexpr int kVK_ANSI_S              = 0x01;
constexpr int kVK_ANSI_D              = 0x02;
constexpr int kVK_ANSI_F              = 0x03;
constexpr int kVK_ANSI_H              = 0x04;
constexpr int kVK_ANSI_G              = 0x05;
constexpr int kVK_ANSI_Z              = 0x06;
constexpr int kVK_ANSI_X              = 0x07;
constexpr int kVK_ANSI_C              = 0x08;
constexpr int kVK_ANSI_V              = 0x09;
constexpr int kVK_ISO_Section         = 0x0A;
constexpr int kVK_ANSI_B              = 0x0B;
constexpr int kVK_ANSI_Q              = 0x0C;
constexpr int kVK_ANSI_W              = 0x0D;
constexpr int kVK_ANSI_E              = 0x0E;
constexpr int kVK_ANSI_R              = 0x0F;
constexpr int kVK_ANSI_Y              = 0x10;
constexpr int kVK_ANSI_T              = 0x11;
constexpr int kVK_ANSI_1              = 0x12;
constexpr int kVK_ANSI_2              = 0x13;
constexpr int kVK_ANSI_3              = 0x14;
constexpr int kVK_ANSI_4              = 0x15;
constexpr int kVK_ANSI_6              = 0x16;
constexpr int kVK_ANSI_5              = 0x17;
constexpr int kVK_ANSI_Equal          = 0x18;
constexpr int kVK_ANSI_9              = 0x19;
constexpr int kVK_ANSI_7              = 0x1A;
constexpr int kVK_ANSI_Minus          = 0x1B;
constexpr int kVK_ANSI_8              = 0x1C;
constexpr int kVK_ANSI_0              = 0x1D;
constexpr int kVK_ANSI_RightBracket   = 0x1E;
constexpr int kVK_ANSI_O              = 0x1F;
constexpr int kVK_ANSI_U              = 0x20;
constexpr int kVK_ANSI_LeftBracket    = 0x21;
constexpr int kVK_ANSI_I              = 0x22;
constexpr int kVK_ANSI_P              = 0x23;
constexpr int kVK_Return              = 0x24;
constexpr int kVK_ANSI_L              = 0x25;
constexpr int kVK_ANSI_J              = 0x26;
constexpr int kVK_ANSI_Quote          = 0x27;
constexpr int kVK_ANSI_K              = 0x28;
constexpr int kVK_ANSI_Semicolon      = 0x29;
constexpr int kVK_ANSI_Backslash      = 0x2A;
constexpr int kVK_ANSI_Comma          = 0x2B;
constexpr int kVK_ANSI_Slash          = 0x2C;
constexpr int kVK_ANSI_N              = 0x2D;
constexpr int kVK_ANSI_M              = 0x2E;
constexpr int kVK_ANSI_Period         = 0x2F;
constexpr int kVK_Tab                 = 0x30;
constexpr int kVK_Space               = 0x31;
constexpr int kVK_ANSI_Grave          = 0x32;
constexpr int kVK_Delete              = 0x33;
constexpr int kVK_Escape              = 0x35;
constexpr int kVK_RightCommand        = 0x36;
constexpr int kVK_Command             = 0x37;
constexpr int kVK_Shift               = 0x38;
constexpr int kVK_CapsLock            = 0x39;
constexpr int kVK_Option              = 0x3A;
constexpr int kVK_Control             = 0x3B;
constexpr int kVK_RightShift          = 0x3C;
constexpr int kVK_RightOption         = 0x3D;
constexpr int kVK_RightControl        = 0x3E;
constexpr int kVK_ANSI_KeypadDecimal  = 0x41;
constexpr int kVK_ANSI_KeypadMultiply = 0x43;
constexpr int kVK_ANSI_KeypadPlus     = 0x45;
constexpr int kVK_ANSI_KeypadClear    = 0x47;
constexpr int kVK_ANSI_KeypadDivide   = 0x4B;
constexpr int kVK_ANSI_KeypadEnter    = 0x4C;
constexpr int kVK_ANSI_KeypadMinus    = 0x4E;
constexpr int kVK_ANSI_KeypadEquals   = 0x51;
constexpr int kVK_ANSI_Keypad0        = 0x52;
constexpr int kVK_ANSI_Keypad1        = 0x53;
constexpr int kVK_ANSI_Keypad2        = 0x54;
constexpr int kVK_ANSI_Keypad3        = 0x55;
constexpr int kVK_ANSI_Keypad4        = 0x56;
constexpr int kVK_ANSI_Keypad5        = 0x57;
constexpr int kVK_ANSI_Keypad6        = 0x58;
constexpr int kVK_ANSI_Keypad7        = 0x59;
constexpr int kVK_ANSI_Keypad8        = 0x5B;
constexpr int kVK_ANSI_Keypad9        = 0x5C;
constexpr int kVK_F5                  = 0x60;
constexpr int kVK_F6                  = 0x61;
constexpr int kVK_F7                  = 0x62;
constexpr int kVK_F3                  = 0x63;
constexpr int kVK_F8                  = 0x64;
constexpr int kVK_F9                  = 0x65;
constexpr int kVK_F11                 = 0x67;
constexpr int kVK_F13                 = 0x69;
constexpr int kVK_F14                 = 0x6B;
constexpr int kVK_F10                 = 0x6D;
constexpr int kVK_F12                 = 0x6F;
constexpr int kVK_F15                 = 0x71;
constexpr int kVK_Help                = 0x72;
constexpr int kVK_Home                = 0x73;
constexpr int kVK_PageUp              = 0x74;
constexpr int kVK_ForwardDelete       = 0x75;
constexpr int kVK_F4                  = 0x76;
constexpr int kVK_End                 = 0x77;
constexpr int kVK_F2                  = 0x78;
constexpr int kVK_PageDown            = 0x79;
constexpr int kVK_F1                  = 0x7A;
constexpr int kVK_LeftArrow           = 0x7B;
constexpr int kVK_RightArrow          = 0x7C;
constexpr int kVK_DownArrow           = 0x7D;
constexpr int kVK_UpArrow             = 0x7E;
// JIS-specific keys (Japanese physical layout). Tokens from
// <Carbon/HIToolbox/Events.h>; values frozen since the Apple Extended
// Keyboard II. Defined here for the same .cpp-TU header-access reason
// as the kVK_ANSI_* / kVK_ISO_Section block above.
constexpr int kVK_JIS_Yen             = 0x5D;
constexpr int kVK_JIS_Underscore      = 0x5E;
constexpr int kVK_JIS_KeypadComma     = 0x5F;
constexpr int kVK_JIS_Eisu            = 0x66;
constexpr int kVK_JIS_Kana            = 0x68;
} // namespace
#endif

#if defined(XR_PLATFORM_APPLE)
// `g_nsEventInputCvar` has external linkage on purpose: the console
// command lives in xrRender_console.cpp (different TU) and writes
// straight into this storage via `extern int g_nsEventInputCvar`.
// 1 = NSEvent pipeline drives keyboard input, 0 = legacy SDL keyboard
// pipeline.
//
// Default returned to 1 in gitea #124 root fix: the NSEvent local
// monitor in macos_cocoa_shim.mm now gates its keyDown/keyUp swallow
// on SDL_IsTextInputActive(), so text input (console, save-name
// dialog, MP chat) flows through SDL as expected while the A.3
// keyCode pipeline still owns gameplay/menu input. See the gate
// comment block in macos_cocoa_shim.mm for the caveat about
// mid-frame text-input toggles.
int g_nsEventInputCvar = 1;

// Apple HID keyCode -> SDL_Scancode mapping.
// Static replica of SDL's internal table in SDL_cocoakeyboard.m. Lets us
// drop SDL from the keyboard event path without changing the engine's
// scancode-based binding system (xr_level_controller.cpp).
// Indices match NSEvent.keyCode values (kVK_* constants from
// <Carbon/HIToolbox/Events.h>). Unmapped entries default to
// SDL_SCANCODE_UNKNOWN.
namespace
{
constexpr SDL_Scancode kNSKeyCodeToSDLScancode[128] = {
    [kVK_ANSI_A]              = SDL_SCANCODE_A,
    [kVK_ANSI_S]              = SDL_SCANCODE_S,
    [kVK_ANSI_D]              = SDL_SCANCODE_D,
    [kVK_ANSI_F]              = SDL_SCANCODE_F,
    [kVK_ANSI_H]              = SDL_SCANCODE_H,
    [kVK_ANSI_G]              = SDL_SCANCODE_G,
    [kVK_ANSI_Z]              = SDL_SCANCODE_Z,
    [kVK_ANSI_X]              = SDL_SCANCODE_X,
    [kVK_ANSI_C]              = SDL_SCANCODE_C,
    [kVK_ANSI_V]              = SDL_SCANCODE_V,
    [kVK_ANSI_B]              = SDL_SCANCODE_B,
    [kVK_ANSI_Q]              = SDL_SCANCODE_Q,
    [kVK_ANSI_W]              = SDL_SCANCODE_W,
    [kVK_ANSI_E]              = SDL_SCANCODE_E,
    [kVK_ANSI_R]              = SDL_SCANCODE_R,
    [kVK_ANSI_Y]              = SDL_SCANCODE_Y,
    [kVK_ANSI_T]              = SDL_SCANCODE_T,
    [kVK_ANSI_1]              = SDL_SCANCODE_1,
    [kVK_ANSI_2]              = SDL_SCANCODE_2,
    [kVK_ANSI_3]              = SDL_SCANCODE_3,
    [kVK_ANSI_4]              = SDL_SCANCODE_4,
    [kVK_ANSI_6]              = SDL_SCANCODE_6,
    [kVK_ANSI_5]              = SDL_SCANCODE_5,
    [kVK_ANSI_Equal]          = SDL_SCANCODE_EQUALS,
    [kVK_ANSI_9]              = SDL_SCANCODE_9,
    [kVK_ANSI_7]              = SDL_SCANCODE_7,
    [kVK_ANSI_Minus]          = SDL_SCANCODE_MINUS,
    [kVK_ANSI_8]              = SDL_SCANCODE_8,
    [kVK_ANSI_0]              = SDL_SCANCODE_0,
    [kVK_ANSI_RightBracket]   = SDL_SCANCODE_RIGHTBRACKET,
    [kVK_ANSI_O]              = SDL_SCANCODE_O,
    [kVK_ANSI_U]              = SDL_SCANCODE_U,
    [kVK_ANSI_LeftBracket]    = SDL_SCANCODE_LEFTBRACKET,
    [kVK_ANSI_I]              = SDL_SCANCODE_I,
    [kVK_ANSI_P]              = SDL_SCANCODE_P,
    [kVK_Return]              = SDL_SCANCODE_RETURN,
    [kVK_ANSI_L]              = SDL_SCANCODE_L,
    [kVK_ANSI_J]              = SDL_SCANCODE_J,
    [kVK_ANSI_Quote]          = SDL_SCANCODE_APOSTROPHE,
    [kVK_ANSI_K]              = SDL_SCANCODE_K,
    [kVK_ANSI_Semicolon]      = SDL_SCANCODE_SEMICOLON,
    [kVK_ANSI_Backslash]      = SDL_SCANCODE_BACKSLASH,
    [kVK_ANSI_Comma]          = SDL_SCANCODE_COMMA,
    [kVK_ANSI_Slash]          = SDL_SCANCODE_SLASH,
    [kVK_ANSI_N]              = SDL_SCANCODE_N,
    [kVK_ANSI_M]              = SDL_SCANCODE_M,
    [kVK_ANSI_Period]         = SDL_SCANCODE_PERIOD,
    [kVK_Tab]                 = SDL_SCANCODE_TAB,
    [kVK_Space]               = SDL_SCANCODE_SPACE,
    [kVK_ANSI_Grave]          = SDL_SCANCODE_GRAVE,
    // ISO European keyboards (включая Mac MacBook RU/EU layouts) имеют
    // отдельную клавишу `§/±/ёЁ` между левым Shift и Z/Я с keyCode 0x0A.
    // SDL на ISO-layout swap'ал её с ANSI_Grave чтобы console-open binding
    // (SCANCODE_GRAVE) попадал на эту физическую клавишу. Мы не делаем
    // runtime ISO detection — просто маппим обе клавиши на GRAVE,
    // обе работают как console-open. Безвредно: ANSI keyboards не
    // имеют этого keyCode.
    [kVK_ISO_Section]         = SDL_SCANCODE_GRAVE,
    [kVK_Delete]              = SDL_SCANCODE_BACKSPACE,
    [kVK_Escape]              = SDL_SCANCODE_ESCAPE,
    [kVK_Command]             = SDL_SCANCODE_LGUI,
    [kVK_Shift]               = SDL_SCANCODE_LSHIFT,
    [kVK_CapsLock]            = SDL_SCANCODE_CAPSLOCK,
    [kVK_Option]              = SDL_SCANCODE_LALT,
    [kVK_Control]             = SDL_SCANCODE_LCTRL,
    [kVK_RightCommand]        = SDL_SCANCODE_RGUI,
    [kVK_RightShift]          = SDL_SCANCODE_RSHIFT,
    [kVK_RightOption]         = SDL_SCANCODE_RALT,
    [kVK_RightControl]        = SDL_SCANCODE_RCTRL,
    [kVK_ANSI_KeypadDecimal]  = SDL_SCANCODE_KP_PERIOD,
    [kVK_ANSI_KeypadMultiply] = SDL_SCANCODE_KP_MULTIPLY,
    [kVK_ANSI_KeypadPlus]     = SDL_SCANCODE_KP_PLUS,
    [kVK_ANSI_KeypadClear]    = SDL_SCANCODE_NUMLOCKCLEAR,
    [kVK_ANSI_KeypadDivide]   = SDL_SCANCODE_KP_DIVIDE,
    [kVK_ANSI_KeypadEnter]    = SDL_SCANCODE_KP_ENTER,
    [kVK_ANSI_KeypadMinus]    = SDL_SCANCODE_KP_MINUS,
    [kVK_ANSI_KeypadEquals]   = SDL_SCANCODE_KP_EQUALS,
    [kVK_ANSI_Keypad0]        = SDL_SCANCODE_KP_0,
    [kVK_ANSI_Keypad1]        = SDL_SCANCODE_KP_1,
    [kVK_ANSI_Keypad2]        = SDL_SCANCODE_KP_2,
    [kVK_ANSI_Keypad3]        = SDL_SCANCODE_KP_3,
    [kVK_ANSI_Keypad4]        = SDL_SCANCODE_KP_4,
    [kVK_ANSI_Keypad5]        = SDL_SCANCODE_KP_5,
    [kVK_ANSI_Keypad6]        = SDL_SCANCODE_KP_6,
    [kVK_ANSI_Keypad7]        = SDL_SCANCODE_KP_7,
    [kVK_ANSI_Keypad8]        = SDL_SCANCODE_KP_8,
    [kVK_ANSI_Keypad9]        = SDL_SCANCODE_KP_9,
    [kVK_F5]                  = SDL_SCANCODE_F5,
    [kVK_F6]                  = SDL_SCANCODE_F6,
    [kVK_F7]                  = SDL_SCANCODE_F7,
    [kVK_F3]                  = SDL_SCANCODE_F3,
    [kVK_F8]                  = SDL_SCANCODE_F8,
    [kVK_F9]                  = SDL_SCANCODE_F9,
    [kVK_F11]                 = SDL_SCANCODE_F11,
    [kVK_F13]                 = SDL_SCANCODE_PRINTSCREEN,
    [kVK_F14]                 = SDL_SCANCODE_SCROLLLOCK,
    [kVK_F10]                 = SDL_SCANCODE_F10,
    [kVK_F12]                 = SDL_SCANCODE_F12,
    [kVK_F15]                 = SDL_SCANCODE_PAUSE,
    [kVK_Help]                = SDL_SCANCODE_INSERT,
    [kVK_Home]                = SDL_SCANCODE_HOME,
    [kVK_PageUp]              = SDL_SCANCODE_PAGEUP,
    [kVK_ForwardDelete]       = SDL_SCANCODE_DELETE,
    [kVK_F4]                  = SDL_SCANCODE_F4,
    [kVK_End]                 = SDL_SCANCODE_END,
    [kVK_F2]                  = SDL_SCANCODE_F2,
    [kVK_PageDown]            = SDL_SCANCODE_PAGEDOWN,
    [kVK_F1]                  = SDL_SCANCODE_F1,
    [kVK_LeftArrow]           = SDL_SCANCODE_LEFT,
    [kVK_RightArrow]          = SDL_SCANCODE_RIGHT,
    [kVK_DownArrow]           = SDL_SCANCODE_DOWN,
    [kVK_UpArrow]             = SDL_SCANCODE_UP,
    // JIS-only keys (Japanese physical layout). No effect on ANSI/ISO
    // keyboards since these keyCodes are never emitted there. Closes
    // the parity gap with SDL2's src/video/cocoa/SDL_cocoakeyboard.m
    // mapping table so VerifyInputTable() reports a clean Apple-side
    // surface.
    [kVK_JIS_Yen]             = SDL_SCANCODE_INTERNATIONAL3,
    [kVK_JIS_Underscore]      = SDL_SCANCODE_INTERNATIONAL1,
    [kVK_JIS_KeypadComma]     = SDL_SCANCODE_KP_COMMA,
    [kVK_JIS_Eisu]            = SDL_SCANCODE_LANG2,
    [kVK_JIS_Kana]            = SDL_SCANCODE_LANG1,
};

void VerifyInputTable()
{
    // Skip in release builds when NSEvent path is off — pointless cost.
    if (!g_nsEventInputCvar)
    {
#ifndef _DEBUG
        return;
#endif
    }

    u32 mapped = 0, unmapped = 0, deadKeysym = 0;
    for (int keyCode = 0; keyCode < 128; ++keyCode)
    {
        const SDL_Scancode our = kNSKeyCodeToSDLScancode[keyCode];
        if (our == SDL_SCANCODE_UNKNOWN)
        {
            ++unmapped;
            // Only warn on keyCodes inside the regions Apple actually
            // emits — i.e. anything below 0x80. Designated-initializer
            // gaps in unused ranges (Apple reserves swathes for future
            // use) silently default to SCANCODE_UNKNOWN; that's expected.
            Msg("! input: Apple keyCode 0x%02X (%d) unmapped — emits SDL_SCANCODE_UNKNOWN", keyCode, keyCode);
            continue;
        }
        const SDL_Keycode ksym = SDL_GetKeyFromScancode(our);
        if (ksym == SDLK_UNKNOWN)
        {
            ++deadKeysym;
            Msg("! input: Apple keyCode 0x%02X (%d) → SDL_Scancode %d → SDLK_UNKNOWN (dead mapping)", keyCode, keyCode, int(our));
            continue;
        }
        ++mapped;
    }
    Msg("* input: Apple keyCode→SDL_Scancode parity scan: mapped=%u unmapped=%u dead=%u", mapped, unmapped, deadKeysym);
}
} // namespace

extern "C" void OpenXRay_VerifyInputTable()
{
    VerifyInputTable();
}
#endif // XR_PLATFORM_APPLE

#if defined(XR_PLATFORM_APPLE)
#include "macos_cocoa_shim.h"

namespace
{
// Last modifierFlags snapshot observed via a FlagsChanged record. Used
// to diff against the next record so we can derive press/release for the
// modifier keys (which never produce KeyDown/KeyUp NSEvents).
uint32_t g_lastShimModifierFlags = 0;

// NSEvent.modifierFlags bit values (from <AppKit/NSEvent.h>). Defined
// locally because xr_input.cpp is a plain .cpp TU and cannot include
// Cocoa headers. The bit positions are ABI-stable since 10.12.
constexpr uint32_t kNSFlagCapsLock = 1u << 16;
constexpr uint32_t kNSFlagShift    = 1u << 17;
constexpr uint32_t kNSFlagControl  = 1u << 18;
constexpr uint32_t kNSFlagOption   = 1u << 19;
constexpr uint32_t kNSFlagCommand  = 1u << 20;

SDL_Scancode ModifierKeyCodeToScancode(uint16_t keyCode)
{
    switch (keyCode)
    {
        case kVK_Shift:         return SDL_SCANCODE_LSHIFT;
        case kVK_RightShift:    return SDL_SCANCODE_RSHIFT;
        case kVK_Control:       return SDL_SCANCODE_LCTRL;
        case kVK_RightControl:  return SDL_SCANCODE_RCTRL;
        case kVK_Option:        return SDL_SCANCODE_LALT;
        case kVK_RightOption:   return SDL_SCANCODE_RALT;
        case kVK_Command:       return SDL_SCANCODE_LGUI;
        case kVK_RightCommand:  return SDL_SCANCODE_RGUI;
        case kVK_CapsLock:      return SDL_SCANCODE_CAPSLOCK;
        default:                return SDL_SCANCODE_UNKNOWN;
    }
}
} // namespace

extern "C" void OpenXRay_SyntheticReleaseAllKeys(void)
{
    if (pInput)
        pInput->IR_ReleaseAll();
}

// Frame-boundary applier for the focus-loss release-all flag (gitea #134).
// Called from OpenXRay_RunPerFrameMacOSHooks (Engine.cpp) on the render thread,
// after the AppKit shim has set g_pendingReleaseAllKeys via
// OpenXRay_RequestReleaseAllKeys(). Two responsibilities:
//   1. pInput->IR_ReleaseAll() — fires IR_OnKeyboard{Release}/IR_OnMouseRelease
//      to the top receiver for every held scancode + mouse button, then clears
//      keyboardState/mouseState. Both pipelines (SDL + NSEvent) share this
//      bitset, so a single release-all unsticks both.
//   2. g_lastShimModifierFlags = 0 — resets the NSEvent FlagsChanged diff
//      baseline. While the app was backgrounded, NSEvent FlagsChanged callbacks
//      did not fire, so the cached snapshot is stale by definition. Zeroing it
//      here means the next FlagsChanged after focus return (or the explicit
//      sync via OpenXRay_SyncModifierFlags in applicationDidBecomeActive:) XORs
//      against a known-clean baseline; otherwise a Cmd held at the moment of
//      Cmd-Tab would leave kNSFlagCommand pinned in the cache, and the first
//      post-return FlagsChanged would either resurrect the released modifier
//      or silently swallow a new press.
extern "C" void OpenXRay_ApplyReleaseAllKeys(void)
{
    if (pInput)
        pInput->IR_ReleaseAll();
    g_lastShimModifierFlags = 0;
}

// Called from applicationDidBecomeActive: in macos_cocoa_shim.mm. Re-aligns
// the FlagsChanged baseline after a focus-loss window: while the app was
// backgrounded NSEvent FlagsChanged callbacks didn't fire, so the cached
// snapshot can be stale. Next FlagsChanged would otherwise XOR against a
// pre-background state and either resurrect a released modifier or hide
// a newly-pressed one.
extern "C" void OpenXRay_SyncModifierFlags(uint32_t flags)
{
    g_lastShimModifierFlags = flags;
}

extern "C" void OpenXRay_OnNSEventInputCvarChanged(int newValue)
{
    const int oldValue = g_nsEventInputCvar;
    g_nsEventInputCvar = newValue;
    if (oldValue != newValue)
    {
        OpenXRay_SyntheticReleaseAllKeys();
        OpenXRay_SetNSEventInputEnabled(newValue);
    }
}
#endif // XR_PLATFORM_APPLE

CInput* pInput = nullptr;

class DummyReceiver : public IInputReceiver
{
public:
    void IR_OnKeyboardPress(int dik) override
    {
        switch (GetBindedAction(dik))
        {
        case kQUIT:
            if (Console)
                Console->Execute("main_menu");
            return;

        case kCONSOLE:
            if (Console)
                Console->Show();
            return;

        case kEDITOR:
            if (Device.b_is_Ready)
                Device.editor().SwitchToNextState();
            return;

        case kRENDER_PLAYGROUND:
            // DummyReceiver is the always-captured fallback. ide's own
            // IR_OnKeyboardPress only fires when ide is the active input
            // receiver (console open or full editor mode), so without this
            // case the playground hotkey only works when the console is up.
            if (Device.b_is_Ready)
                Device.editor().TogglePlayground();
            return;
        }
    }
} dummyController;

ENGINE_API float psMouseSens = 1.f;
ENGINE_API float psMouseSensScale = 1.f;
ENGINE_API Flags32 psMouseInvert = {};

ENGINE_API float psControllerStickSensX = 0.12f;
ENGINE_API float psControllerStickSensY = 0.7f;
ENGINE_API float psControllerStickSensScale = 1.f;
ENGINE_API float psControllerStickInnerDeadZone = 0.15f;
ENGINE_API float psControllerStickOuterDeadZone = 0.96f;
ENGINE_API float psControllerStickAngularDeadZone = 0.95f;
ENGINE_API float psControllerSensorSens = 0.5f;
ENGINE_API float psControllerSensorDeadZone = 0.005f;
ENGINE_API Flags32 psControllerFlags = { ControllerEnableSensors };

ENGINE_API float psControllerCursorAutohideTime = 1.5f;

static bool AltF4Pressed = false;

// Max events per frame
constexpr size_t MAX_KEYBOARD_EVENTS = 64;
constexpr size_t MAX_MOUSE_EVENTS = 256;
constexpr size_t MAX_CONTROLLER_EVENTS = 256;

CInput::CInput(const bool exclusive)
{
    ZoneScoped;

    exclusiveInput = exclusive;

    Log("Starting INPUT device...");

#if defined(XR_PLATFORM_APPLE)
    textInputBackend = CreateNativeTextInputBackend();
#else
    textInputBackend = CreateSDLTextInputBackend();
#endif

    mouseState.reset();
    keyboardState.reset();

    //===================== Dummy pack
    iCapture(&dummyController);

    SDL_SetHint(SDL_HINT_WINDOWS_NO_CLOSE_ON_ALT_F4, "1"); // We need to handle it manually

    Device.seqAppActivate.Add(this);
    Device.seqAppDeactivate.Add(this, REG_PRIORITY_HIGH);
    Device.seqFrame.Add(this, REG_PRIORITY_HIGH);

    mouseCursors[SDL_SYSTEM_CURSOR_ARROW]     = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW);
    mouseCursors[SDL_SYSTEM_CURSOR_IBEAM]     = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_IBEAM);
    mouseCursors[SDL_SYSTEM_CURSOR_WAIT]      = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_WAIT);
    mouseCursors[SDL_SYSTEM_CURSOR_CROSSHAIR] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_CROSSHAIR);
    mouseCursors[SDL_SYSTEM_CURSOR_WAITARROW] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_WAITARROW);
    mouseCursors[SDL_SYSTEM_CURSOR_SIZENWSE]  = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZENWSE);
    mouseCursors[SDL_SYSTEM_CURSOR_SIZENESW]  = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZENESW);
    mouseCursors[SDL_SYSTEM_CURSOR_SIZEWE]    = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZEWE);
    mouseCursors[SDL_SYSTEM_CURSOR_SIZENS]    = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZENS);
    mouseCursors[SDL_SYSTEM_CURSOR_SIZEALL]   = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZEALL);
    mouseCursors[SDL_SYSTEM_CURSOR_NO]        = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_NO);
    mouseCursors[SDL_SYSTEM_CURSOR_HAND]      = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_HAND);

    for (int i = 0; i < SDL_NumJoysticks(); ++i)
        OpenController(i);
}

CInput::~CInput()
{
    ZoneScoped;

    GrabInput(false);

    for (auto& controller : controllers)
        SDL_GameControllerClose(controller);

    for (auto& cursor : mouseCursors)
    {
        SDL_FreeCursor(cursor);
        cursor = nullptr;
    }
    lastCursor = nullptr;

    Device.seqFrame.Remove(this);
    Device.seqAppDeactivate.Remove(this);
    Device.seqAppActivate.Remove(this);
}

void CInput::OpenController(int idx)
{
    if (!SDL_IsGameController(idx))
        return;

    const auto controller = SDL_GameControllerOpen(idx);
    if (!controller)
        return;

    if (psControllerFlags.test(ControllerEnableSensors))
        SDL_GameControllerSetSensorEnabled(controller, SDL_SENSOR_GYRO, SDL_TRUE);

    controllers.emplace_back(controller);
}

//-----------------------------------------------------------------------

void CInput::DumpStatistics(IGameFont& font, IPerformanceAlert* alert)
{
    font.OutNext("*** INPUT:    %2.2fms", GetStats().FrameTime.result);
}

void CInput::SetCurrentInputType(InputType type)
{
    currentInputType = type;

    switch (type)
    {
    case KeyboardMouse:
        controllerState.id = -1;
        if (psControllerFlags.test(ControllerEnableSensors))
        {
            for (auto controller : controllers)
                SDL_GameControllerSetSensorEnabled(controller, SDL_SENSOR_GYRO, SDL_FALSE);
        }
        break;

    case Controller:
        if (psControllerFlags.test(ControllerEnableSensors))
        {
            for (auto controller : controllers)
                SDL_GameControllerSetSensorEnabled(controller, SDL_SENSOR_GYRO, SDL_TRUE);
        }
        break;
    }
    // Always flush it. On the first controller invocation,
    // prefer to receive sensor updates "from scratch",
    // on the next frame.
    SDL_FlushEvent(SDL_CONTROLLERSENSORUPDATE);
}

void CInput::MouseUpdate()
{
    ZoneScoped;

    // Mouse2 is a middle button in SDL,
    // but in X-Ray this is a right button
    constexpr int RemapIdx[] = { 0, 2, 1, 3, 4 };
    constexpr int IdxToKey[] = { MOUSE_1, MOUSE_2, MOUSE_3, MOUSE_4, MOUSE_5 };
    static_assert(std::size(RemapIdx) == COUNT_MOUSE_BUTTONS);
    static_assert(std::size(IdxToKey) == COUNT_MOUSE_BUTTONS);

    const auto mousePrev = mouseState;
    mouseAxisState[2] = 0;
    mouseAxisState[3] = 0;

    // SDL drain block — same Apple-gate split pattern as KeyUpdate. With
    // nsevent_input=1 the NSEvent local monitor consumes mouse/scroll events
    // before SDL sees them; the drain in NSEventDrain() maintains mouseState
    // and emits IR_OnMouse* callbacks. The per-frame hold loop below must run
    // unconditionally so mouseState (populated by either source) drives the
    // IR_OnMouseHold callbacks (continuous-fire while LMB held, etc.).
    bool runSDLDrain = true;
#if defined(XR_PLATFORM_APPLE)
    if (g_nsEventInputCvar)
        runSDLDrain = false;
#endif

    if (runSDLDrain)
    {
        bool mouseMoved = false;
        int offs[2]{};
        float scroll[2]{};

        SDL_Event events[MAX_MOUSE_EVENTS];
        SDL_PumpEvents();
        const auto count = SDL_PeepEvents(events, MAX_MOUSE_EVENTS,
            SDL_GETEVENT, SDL_MOUSEMOTION, SDL_MOUSEWHEEL);

        for (int i = 0; i < count; ++i)
        {
            const SDL_Event& event = events[i];

            switch (event.type)
            {
            case SDL_MOUSEMOTION:
                mouseMoved = true;
                offs[0] += event.motion.xrel;
                offs[1] += event.motion.yrel;
                mouseAxisState[0] = event.motion.x;
                mouseAxisState[1] = event.motion.y;
                break;

            case SDL_MOUSEBUTTONDOWN:
            {
                const auto idx = RemapIdx[event.button.button - 1];
                mouseState[idx] = true;
                cbStack.back()->IR_OnMousePress(IdxToKey[idx]);
                break;
            }
            case SDL_MOUSEBUTTONUP:
            {
                const auto idx = RemapIdx[event.button.button - 1];
                mouseState[idx] = false;
                cbStack.back()->IR_OnMouseRelease(IdxToKey[idx]);
                break;
            }
            case SDL_MOUSEWHEEL:
                mouseMoved = true;
                scroll[0] += event.wheel.preciseX;
                scroll[1] += event.wheel.preciseY;
                mouseAxisState[2] += event.wheel.x;
                mouseAxisState[3] += event.wheel.y;
                break;
            }
        }

        if (mouseMoved)
        {
            if (offs[0] || offs[1])
                cbStack.back()->IR_OnMouseMove(offs[0], offs[1]);

            if (!fis_zero(scroll[0]) || !fis_zero(scroll[1]))
                cbStack.back()->IR_OnMouseWheel(scroll[0], scroll[1]);
        }
    }

    for (int i = 0; i < MOUSE_COUNT; ++i)
    {
        if (mouseState[i] && mousePrev[i])
            cbStack.back()->IR_OnMouseHold(IdxToKey[i]);
    }
}

void CInput::KeyUpdate()
{
    ZoneScoped;

    // Drain SDL keyboard events.
    //
    // On Apple (A.7.2, gitea #165) the SDL queue drain is disabled
    // entirely: keyDown has one ingest path now — the A.3 NSEvent
    // local monitor in macos_cocoa_shim.mm. In gameplay mode the
    // monitor swallows the event and pushes a ring-queue record
    // (drained by NSEventDrain in OnFrame -> IR_OnKeyboardPress); in
    // text-input mode the monitor hands the event to
    // [NSTextInputContext handleEvent:] (NativeTextInputBackend.mm),
    // which lands committed UTF-8 in IR_OnTextInput on the top
    // receiver. SDL still receives keyDowns in parallel via the
    // [NSWindow keyDown:] responder chain (parallel ingest, see
    // notes/decisions/a6-textinput-contract.md), but with no consumer
    // here the SDL queue is inert: events accumulate harmlessly and
    // are never read.
    //
    // The per-frame hold loop at the end runs unconditionally —
    // keyboardState is populated by whichever source is active.
#if !defined(XR_PLATFORM_APPLE)
    {
        SDL_Event events[MAX_KEYBOARD_EVENTS];
        const auto count = SDL_PeepEvents(events, MAX_KEYBOARD_EVENTS,
            SDL_GETEVENT, SDL_KEYDOWN, SDL_KEYMAPCHANGED);

        // Let iGetAsyncKeyState work correctly during this frame immediately
        for (int i = 0; i < count; ++i)
        {
            SDL_Event& event = events[i];

            switch (event.type)
            {
            case SDL_KEYDOWN:
                if (event.key.repeat)
                    continue;
                keyboardState[event.key.keysym.scancode] = true;
                break;

            case SDL_KEYUP:
                keyboardState[event.key.keysym.scancode] = false;
                break;
            }
        }

        if (keyboardState[SDL_SCANCODE_F4] && (keyboardState[SDL_SCANCODE_LALT] || keyboardState[SDL_SCANCODE_RALT]))
        {
            AltF4Pressed = true;
            Engine.Event.Defer("KERNEL:disconnect");
            Engine.Event.Defer("KERNEL:quit");
            return;
        }

        if (count)
            SetCurrentInputType(KeyboardMouse);

        // If textInputCounter has changed,
        // we assume that text input target changed.
        // Theoretically, this is not always true, though.
        // But we always can change the solution.
        // If we find out something not work as expected.
        const auto cnt = textInputCounter;

        for (int i = 0; i < count; ++i)
        {
            const SDL_Event& event = events[i];

            switch (event.type)
            {
            case SDL_KEYDOWN:
                if (event.key.repeat)
                    continue;
                cbStack.back()->IR_OnKeyboardPress(event.key.keysym.scancode);
                break;

            case SDL_KEYUP:
                cbStack.back()->IR_OnKeyboardRelease(event.key.keysym.scancode);
                break;

            case SDL_TEXTINPUT:
                if (cnt != textInputCounter)
                    continue; // if input target changed, skip this frame
                cbStack.back()->IR_OnTextInput(event.text.text);
                break;

            case SDL_KEYMAPCHANGED:
                seqKeyMapChanged.Process();
                break;
            }
        }
    }
#endif

    for (u32 i = 0; i < COUNT_KB_BUTTONS; ++i)
        if (keyboardState[i])
            cbStack.back()->IR_OnKeyboardHold(i);
}

bool ControllerState::attitude_changed() const
{
    // XXX: maybe check if magnitude is 0 instead?
    return gyroscope.similar(Fvector{ 0.f, 0.f, 0.f }, psControllerSensorDeadZone);
}

void CInput::ControllerUpdate()
{
    ZoneScoped;

    constexpr int ControllerButtonToKey[] =
    {
        XR_CONTROLLER_BUTTON_A,
        XR_CONTROLLER_BUTTON_B,
        XR_CONTROLLER_BUTTON_X,
        XR_CONTROLLER_BUTTON_Y,
        XR_CONTROLLER_BUTTON_BACK,
        XR_CONTROLLER_BUTTON_GUIDE,
        XR_CONTROLLER_BUTTON_START,
        XR_CONTROLLER_BUTTON_LEFTSTICK,
        XR_CONTROLLER_BUTTON_RIGHTSTICK,
        XR_CONTROLLER_BUTTON_LEFTSHOULDER,
        XR_CONTROLLER_BUTTON_RIGHTSHOULDER,
        XR_CONTROLLER_BUTTON_DPAD_UP,
        XR_CONTROLLER_BUTTON_DPAD_DOWN,
        XR_CONTROLLER_BUTTON_DPAD_LEFT,
        XR_CONTROLLER_BUTTON_DPAD_RIGHT,
        XR_CONTROLLER_BUTTON_MISC1,
        XR_CONTROLLER_BUTTON_PADDLE1,
        XR_CONTROLLER_BUTTON_PADDLE2,
        XR_CONTROLLER_BUTTON_PADDLE3,
        XR_CONTROLLER_BUTTON_PADDLE4,
        XR_CONTROLLER_BUTTON_TOUCHPAD,
    };

    SDL_Event events[MAX_CONTROLLER_EVENTS];
    auto count = SDL_PeepEvents(events, MAX_CONTROLLER_EVENTS,
        SDL_GETEVENT, SDL_CONTROLLERDEVICEADDED, SDL_CONTROLLERDEVICEREMAPPED);

    for (int i = 0; i < count; ++i)
    {
        const SDL_Event& event = events[i];
        switch (event.type)
        {
        case SDL_CONTROLLERDEVICEADDED:
            OpenController(event.cdevice.which);
            break;

        case SDL_CONTROLLERDEVICEREMOVED:
        {
            const auto controller = SDL_GameControllerFromInstanceID(event.cdevice.which);
            const auto it = std::find(controllers.begin(), controllers.end(), controller);
            if (it != controllers.end())
                controllers.erase(it);
            break;
        }

        case SDL_CONTROLLERDEVICEREMAPPED:
            // We are skipping it,
            // but it's in the SDL_PeepEvents call
            // to make sure it's removed from event queue
            break;
        } // switch (event.type)
    }

    if (!IsControllerAvailable())
        return;

    count = SDL_PeepEvents(nullptr, 0,
        SDL_PEEKEVENT, SDL_CONTROLLERAXISMOTION, SDL_CONTROLLERTOUCHPADUP);

    if (count)
        SetCurrentInputType(Controller);
    else if (currentInputType != Controller)
        return;

    SDL_PumpEvents();
    count = SDL_PeepEvents(events, MAX_CONTROLLER_EVENTS,
        SDL_GETEVENT, SDL_CONTROLLERAXISMOTION, SDL_CONTROLLERSENSORUPDATE);

    constexpr ControllerAxisState pressedAxis{ 1.0f };
    constexpr ControllerAxisState releasedAxis{};

    static_assert(SDL_CONTROLLER_AXIS_MAX == 6, "Align the depending code with the changes in SDL_GameControllerAxis.");
    static float axes[SDL_CONTROLLER_AXIS_MAX]{};
    bool axisMoved[SDL_CONTROLLER_AXIS_MAX]{};
    const auto controllerPrev = controllerState;

    for (int i = 0; i < count; ++i)
    {
        const SDL_Event& event = events[i];

        switch (event.type)
        {
        case SDL_CONTROLLERAXISMOTION:
        {
            if (controllerState.id != event.caxis.which) // don't write if don't really need to
                controllerState.id = event.caxis.which;

            axisMoved[event.caxis.axis] = true;
            axes[event.caxis.axis] = event.caxis.value;
            break;
        }

        case SDL_CONTROLLERBUTTONDOWN:
            if (controllerState.id != event.cbutton.which) // don't write if don't really need to
                controllerState.id = event.cbutton.which;

            controllerState.buttons[event.cbutton.button] = true;
            cbStack.back()->IR_OnControllerPress(ControllerButtonToKey[event.cbutton.button], pressedAxis);
            break;

        case SDL_CONTROLLERBUTTONUP:
            if (controllerState.id != event.cbutton.which) // don't write if don't really need to
                controllerState.id = event.cbutton.which;

            controllerState.buttons[event.cbutton.button] = false;
            cbStack.back()->IR_OnControllerRelease(ControllerButtonToKey[event.cbutton.button], releasedAxis);
            break;

        case SDL_CONTROLLERSENSORUPDATE:
        {
            if (controllerState.id != event.csensor.which)
                break; // only use data from the recently used controller
            if (event.csensor.sensor != SDL_SENSOR_GYRO)
                break;

            controllerState.gyroscope = Fvector{ -event.csensor.data[1], -event.csensor.data[0], -event.csensor.data[2] };
            if (controllerState.attitude_changed())
                cbStack.back()->IR_OnControllerAttitudeChange(controllerState.gyroscope);
            break;
        }
        } // switch (event.type)
    }

    for (int i = 0; i < XR_CONTROLLER_BUTTON_COUNT; ++i)
    {
        if (controllerState.buttons[i] && controllerPrev.buttons[i])
            cbStack.back()->IR_OnControllerHold(ControllerButtonToKey[i], pressedAxis);
    }

    const float innerDeadZone = psControllerStickInnerDeadZone * SDL_JOYSTICK_AXIS_MAX;
    const float outerDeadZone = psControllerStickOuterDeadZone * SDL_JOYSTICK_AXIS_MAX;

    const auto applyStickDeadZone = [&](Fvector2 axis) -> ControllerAxisState
    {
        float magnitude = axis.magnitude();

        if (magnitude <= innerDeadZone || psControllerStickInnerDeadZone >= 1.0f)
            return {};

        axis.div(magnitude);

        if (magnitude > outerDeadZone)
            magnitude = outerDeadZone;

        const float normalizedMagnitude = (magnitude - innerDeadZone) / (outerDeadZone - innerDeadZone);
        axis.mul(normalizedMagnitude);
        return { axis, normalizedMagnitude };
    };

    const auto applyTriggerDeadZone = [](float value) -> ControllerAxisState
    {
        return value / SDL_JOYSTICK_AXIS_MAX;
    };

    if (axisMoved[0] || axisMoved[1])
        controllerState.axis.left = applyStickDeadZone({ axes[0], axes[1] });
    if (axisMoved[2] || axisMoved[3])
        controllerState.axis.right = applyStickDeadZone({ axes[2], axes[3] });
    if (axisMoved[4])
        controllerState.axis.trigger_left = applyTriggerDeadZone(axes[4]);
    if (axisMoved[5])
        controllerState.axis.trigger_right = applyTriggerDeadZone(axes[5]);

    const auto checkAxis = [this](int axis, const ControllerAxisState& state, const ControllerAxisState& prevState)
    {
        const bool isActive = !fis_zero(state.magnitude);
        const bool isPrevActive = !fis_zero(prevState.magnitude);

        if (isActive && isPrevActive)
            cbStack.back()->IR_OnControllerHold(axis, state);
        else if (isActive)
            cbStack.back()->IR_OnControllerPress(axis, state);
        else if (isPrevActive)
            cbStack.back()->IR_OnControllerRelease(axis, state);
    };

    checkAxis(XR_CONTROLLER_AXIS_LEFT,          controllerState.axis.left,          controllerPrev.axis.left);
    checkAxis(XR_CONTROLLER_AXIS_RIGHT,         controllerState.axis.right,         controllerPrev.axis.right);
    checkAxis(XR_CONTROLLER_AXIS_TRIGGER_LEFT,  controllerState.axis.trigger_left,  controllerPrev.axis.trigger_left);
    checkAxis(XR_CONTROLLER_AXIS_TRIGGER_RIGHT, controllerState.axis.trigger_right, controllerPrev.axis.trigger_right);
}

bool KbdKeyToButtonName(const int dik, xr_string& result)
{
    if (dik >= 0)
    {
        // SDL_GetScancodeName gives the physical-key label, always in
        // English ("E", "Space", "Left Ctrl"), independent of the active
        // keyboard layout. That's what the binding UI wants:
        // "press [E] to open door" should stay Latin even when the player
        // is typing save names in Russian -- text input keeps using
        // SDL_TEXTINPUT which respects the layout, so this only affects
        // the binding labels.
        cpcstr name = SDL_GetScancodeName((SDL_Scancode)dik);
        if (name && name[0])
        {
            result = name;
            return true;
        }
    }

    return false;
}

bool OtherDevicesKeyToButtonName(const int btn, xr_string& /*result*/)
{
    if (btn > CInput::COUNT_KB_BUTTONS)
    {
        // XXX: Not implemented
        return false;
    }

    return false;
}

bool CInput::GetKeyName(const int dik, pstr dest_str, int dest_sz)
{
    xr_string keyname;
    bool result;

    if (dik < COUNT_KB_BUTTONS)
        result = KbdKeyToButtonName(dik, keyname);
    else
        result = OtherDevicesKeyToButtonName(dik, keyname);

    if (keyname.empty())
        return false;

    xr_strcpy(dest_str, dest_sz, keyname.c_str());
    return result;
}

bool CInput::iGetAsyncKeyState(const int key)
{
    if (key < COUNT_KB_BUTTONS)
        return keyboardState[key];

    if (key > MOUSE_INVALID && key < MOUSE_MAX)
    {
        const int idx = key - (MOUSE_INVALID + 1);
        return mouseState[idx];
    }

    if (key > XR_CONTROLLER_BUTTON_INVALID && key < XR_CONTROLLER_BUTTON_MAX)
    {
        const int idx = key - (XR_CONTROLLER_BUTTON_INVALID + 1);
        return controllerState.buttons[idx];
    }

    if (key > XR_CONTROLLER_AXIS_INVALID && key < XR_CONTROLLER_AXIS_MAX)
    {
        return !fis_zero(controllerState.get_axis(key).magnitude);
    }

    // unknown key ???
    return false;
}

void CInput::iGetAsyncScrollPos(Ivector2& p) const
{
    p = { mouseAxisState[2], mouseAxisState[3] };
}

bool CInput::iGetAsyncMousePos(Ivector2& p, bool global /*= false*/) const
{
    if (global)
    {
#if SDL_HAS_CAPTURE_AND_GLOBAL_MOUSE
        SDL_GetGlobalMouseState(&p.x, &p.y);
        return true;
#endif
        // if SDL_HAS_CAPTURE_AND_GLOBAL_MOUSE unavailable
        // fallback to SDL_GetMouseState
        // but report false
    }
    SDL_GetMouseState(&p.x, &p.y);
    return !global;
}

bool CInput::iSetMousePos(const Ivector2& p, bool global /*= false*/) const
{
    if (global)
    {
#if SDL_HAS_CAPTURE_AND_GLOBAL_MOUSE
        SDL_WarpMouseGlobal(p.x, p.y);
        return true;
#endif
        // if SDL_HAS_CAPTURE_AND_GLOBAL_MOUSE unavailable
        // fallback to SDL_WarpMouseInWindow
        // but report false
    }

    SDL_WarpMouseInWindow(Device.m_sdlWnd, p.x, p.y);
    return !global;
}

void CInput::GrabInput(const bool grab)
{
    // Self descriptive
    ShowCursor(!grab);

    // Clip cursor to the current window
    // If SDL_HINT_GRAB_KEYBOARD is set then the keyboard will be grabbed too
    SDL_SetWindowGrab(Device.m_sdlWnd, grab ? SDL_TRUE : SDL_FALSE);

    // Grab the mouse
    if (exclusiveInput)
    {
        SDL_SetRelativeMouseMode(grab ? SDL_TRUE : SDL_FALSE);
#if defined(XR_PLATFORM_APPLE)
        // Mirror capture state into the shim so the NSEvent local monitor's
        // mouse translator picks the right coordinate source: deltas vs
        // absolute pixel coords. Без этого NSEvent path в captured режиме
        // продолжал бы слать locX/Y и менюшный курсор бы прыгал в captured
        // mode (или look-around игнорировал бы deltas).
        OpenXRay_SetMouseCaptureMode(grab ? 1 : 0);
#endif
    }

    // We're done here.
    inputGrabbed = grab;
}

bool CInput::InputIsGrabbed() const
{
    return inputGrabbed;
}

void CInput::ShowCursor(const bool show)
{
    SDL_ShowCursor(show ? SDL_TRUE : SDL_FALSE);
}

void CInput::SetCursor(const SDL_SystemCursor cursor)
{
    SDL_Cursor* expected_cursor = mouseCursors[cursor] ? mouseCursors[cursor] : mouseCursors[ImGuiMouseCursor_Arrow];
    if (lastCursor != expected_cursor) // SDL function doesn't have an early out
    {
        SDL_SetCursor(expected_cursor);
        lastCursor = expected_cursor;
    }
}

void CInput::EnableTextInput()
{
    ++textInputCounter;

    if (textInputCounter == 1)
        textInputBackend->Start();
}

void CInput::DisableTextInput()
{
    --textInputCounter;
    if (textInputCounter < 0)
    {
        Msg("! [text-input] DisableTextInput underflow — counter clamped");
        textInputCounter = 0;
    }

    if (textInputCounter == 0)
        textInputBackend->Stop();
}

bool CInput::IsTextInputEnabled() const
{
    return textInputCounter > 0;
}

void CInput::RegisterKeyMapChangeWatcher(pureKeyMapChanged* watcher, int priority /*= REG_PRIORITY_NORMAL*/)
{
    seqKeyMapChanged.Add(watcher, priority);
}

void CInput::RemoveKeyMapChangeWatcher(pureKeyMapChanged* watcher)
{
    seqKeyMapChanged.Remove(watcher);
}

void CInput::iCapture(IInputReceiver* p)
{
    VERIFY(p);

    // change focus
    if (!cbStack.empty())
        cbStack.back()->IR_OnDeactivate();
    cbStack.push_back(p);
    cbStack.back()->IR_OnActivate();

    // prepare for _new_ controller
    controllerState = {};
}

void CInput::iRelease(IInputReceiver* p)
{
    if (p == cbStack.back())
    {
        cbStack.back()->IR_OnDeactivate();
        cbStack.pop_back();
        cbStack.back()->IR_OnActivate();
    }
    else
    {
        // we are not topmost receiver, so remove the nearest one
        for (size_t cnt = cbStack.size(); cnt > 0; --cnt)
            if (cbStack[cnt - 1] == p)
            {
                xr_vector<IInputReceiver*>::iterator it = cbStack.begin();
                std::advance(it, cnt - 1);
                cbStack.erase(it);
                break;
            }
    }
}

#if defined(XR_PLATFORM_APPLE)
void CInput::NSEventDrain()
{
    OpenXRayNSEventRecord records[64];
    size_t n;
    while ((n = OpenXRay_DrainNSEventQueue(records, 64)) > 0)
    {
        for (size_t i = 0; i < n; ++i)
        {
            const auto& r = records[i];
            if (cbStack.empty())
                continue;
            IInputReceiver* receiver = cbStack.back();

            switch (r.kind)
            {
            case OXR_NS_EVENT_KEY_DOWN:
            {
                if (r.isARepeat)
                    break; // auto-repeat не транслируем (SDL фильтрует так же)
                const SDL_Scancode sc = (r.keyCode<128)
                    ? kNSKeyCodeToSDLScancode[r.keyCode]
                    : SDL_SCANCODE_UNKNOWN;
                if (sc != SDL_SCANCODE_UNKNOWN)
                {
                    // Mirror SDL path (line ~632): per-frame hold loop в
                    // KeyUpdate читает keyboardState[], так что без записи
                    // hold-based actions (движение WASD, sprint Shift+W)
                    // не работают — press проходит, но Hold не догоняет.
                    keyboardState[sc] = true;
                    receiver->IR_OnKeyboardPress((int)sc);
                }
                break;
            }
            case OXR_NS_EVENT_KEY_UP:
            {
                const SDL_Scancode sc = (r.keyCode<128)
                    ? kNSKeyCodeToSDLScancode[r.keyCode]
                    : SDL_SCANCODE_UNKNOWN;
                if (sc != SDL_SCANCODE_UNKNOWN)
                {
                    keyboardState[sc] = false;
                    receiver->IR_OnKeyboardRelease((int)sc);
                }
                break;
            }
            case OXR_NS_EVENT_FLAGS_CHANGED:
            {
                const uint32_t diff = r.modifierFlags ^ g_lastShimModifierFlags;
                // CapsLock is a toggle — synthesize down+up to drive bindings.
                if (diff & kNSFlagCapsLock)
                {
                    receiver->IR_OnKeyboardPress((int)SDL_SCANCODE_CAPSLOCK);
                    receiver->IR_OnKeyboardRelease((int)SDL_SCANCODE_CAPSLOCK);
                }
                // For real modifier keys disambiguate L/R via keyCode and
                // derive direction from the corresponding flag bit's state.
                if (diff & (kNSFlagShift | kNSFlagControl | kNSFlagOption | kNSFlagCommand))
                {
                    const SDL_Scancode sc = ModifierKeyCodeToScancode(r.keyCode);
                    if (sc != SDL_SCANCODE_UNKNOWN)
                    {
                        uint32_t flagBit = 0;
                        switch (r.keyCode)
                        {
                        case kVK_Shift:
                        case kVK_RightShift:    flagBit = kNSFlagShift;   break;
                        case kVK_Control:
                        case kVK_RightControl:  flagBit = kNSFlagControl; break;
                        case kVK_Option:
                        case kVK_RightOption:   flagBit = kNSFlagOption;  break;
                        case kVK_Command:
                        case kVK_RightCommand:  flagBit = kNSFlagCommand; break;
                        default: break;
                        }
                        if (flagBit && (r.modifierFlags & flagBit))
                        {
                            keyboardState[sc] = true;
                            receiver->IR_OnKeyboardPress((int)sc);
                        }
                        else if (flagBit)
                        {
                            keyboardState[sc] = false;
                            receiver->IR_OnKeyboardRelease((int)sc);
                        }
                    }
                }
                g_lastShimModifierFlags = r.modifierFlags;
                break;
            }
            // Mouse buttons map: NSEvent.mouseButton (0=left, 1=right, 2=other)
            // -> MOUSE_1 / MOUSE_2 / MOUSE_3 via MOUSE_INVALID+1+idx. mouseState
            // index follows the same idx (same convention as IR_ReleaseAll in
            // 2a). Note: NSEvent already maps middle button to "other", so the
            // SDL-side RemapIdx (which swaps SDL's middle<->right) is NOT
            // applied here.
            case OXR_NS_EVENT_MOUSE_MOVE:
            case OXR_NS_EVENT_MOUSE_DRAGGED:
            {
                // record содержит ИЛИ deltaX/Y (captured) ИЛИ locX/Y (absolute).
                // Shim сам выбирает на основе g_mouseCaptured; здесь просто
                // передаём что было записано.
                int dx, dy;
                if (r.deltaX != 0.0f || r.deltaY != 0.0f)
                {
                    dx = (int)r.deltaX;
                    dy = (int)r.deltaY;
                }
                else
                {
                    dx = (int)r.locX;
                    dy = (int)r.locY;
                    // Non-captured mode delivers absolute pixel coords —
                    // expose them through mouseAxisState[0..1] so menu code
                    // pathways which read iGetAsyncMousePos фактически
                    // получают актуальную позицию.
                    mouseAxisState[0] = dx;
                    mouseAxisState[1] = dy;
                }
                receiver->IR_OnMouseMove(dx, dy);
                break;
            }
            case OXR_NS_EVENT_MOUSE_DOWN:
            {
                if (r.mouseButton < COUNT_MOUSE_BUTTONS)
                {
                    const int btn = (int)MOUSE_INVALID + 1 + (int)r.mouseButton;
                    mouseState[r.mouseButton] = true;
                    receiver->IR_OnMousePress(btn);
                }
                break;
            }
            case OXR_NS_EVENT_MOUSE_UP:
            {
                if (r.mouseButton < COUNT_MOUSE_BUTTONS)
                {
                    const int btn = (int)MOUSE_INVALID + 1 + (int)r.mouseButton;
                    mouseState[r.mouseButton] = false;
                    receiver->IR_OnMouseRelease(btn);
                }
                break;
            }
            case OXR_NS_EVENT_SCROLL_WHEEL:
                receiver->IR_OnMouseWheel((int)r.deltaX, (int)r.deltaY);
                break;
            }
        }
    }
}
#endif // XR_PLATFORM_APPLE

void CInput::IR_ReleaseAll()
{
    if (cbStack.empty())
        return;
    IInputReceiver* receiver = cbStack.back();

    // Keyboard: release every scancode currently marked as held.
    for (u32 sc = 0; sc < COUNT_KB_BUTTONS; ++sc)
    {
        if (keyboardState[sc])
            receiver->IR_OnKeyboardRelease((int)sc);
    }

    // Mouse buttons: release every button currently marked as held. Indices
    // match MouseUpdate()'s IdxToKey[] mapping (idx 0..COUNT_MOUSE_BUTTONS-1 →
    // MOUSE_INVALID+1+idx, i.e. MOUSE_1..MOUSE_5).
    for (int idx = 0; idx < COUNT_MOUSE_BUTTONS; ++idx)
    {
        if (mouseState[idx])
            receiver->IR_OnMouseRelease(MOUSE_INVALID + 1 + idx);
    }

    ClearKeyboardState();
}

void CInput::ClearKeyboardState()
{
    keyboardState.reset();
    mouseState.reset();
}

void CInput::OnAppActivate(void)
{
    if (CurrentIR())
        CurrentIR()->IR_OnActivate();

    mouseState.reset();
    keyboardState.reset();
    controllerState = {};
}

void CInput::OnAppDeactivate(void)
{
    if (CurrentIR())
        CurrentIR()->IR_OnDeactivate();

    mouseState.reset();
    keyboardState.reset();
    controllerState = {};
}

void CInput::OnFrame(void)
{
    ZoneScoped;

    if (AltF4Pressed)
        return;

    stats.FrameStart();
    stats.FrameTime.Begin();

#if defined(XR_PLATFORM_APPLE)
    if (g_nsEventInputCvar)
        NSEventDrain();
#endif

    if (Device.dwPrecacheFrame == 0 && !Device.IsAnselActive)
    {
        ControllerUpdate();
        KeyUpdate();
        MouseUpdate();
    }

    stats.FrameTime.End();
    stats.FrameEnd();
}

IInputReceiver* CInput::CurrentIR()
{
    if (cbStack.size())
        return cbStack.back();

    return nullptr;
}

void CInput::ExclusiveMode(const bool exclusive)
{
    GrabInput(false);

    exclusiveInput = exclusive;

    GrabInput(true);
}

bool CInput::IsExclusiveMode() const
{
    return exclusiveInput;
}

void CInput::Feedback(FeedbackType type, float s1, float s2, float duration)
{
    const u16 s1_rumble = iFloor(u16(-1) * clampr(s1, 0.0f, 1.0f));
    const u16 s2_rumble = iFloor(u16(-1) * clampr(s2, 0.0f, 1.0f));
    const u32 duration_ms = duration < 0.f ? 0 : iFloor(duration * 1000.f);

    switch (type)
    {
    case FeedbackController:
    {
        if (controllerState.id != -1)
        {
            const auto controller = SDL_GameControllerFromInstanceID(controllerState.id);
            SDL_GameControllerRumble(controller, s1_rumble, s2_rumble, duration_ms);
        }
        break;
    }

    case FeedbackTriggers:
    {
        if (controllerState.id != -1)
        {
            const auto controller = SDL_GameControllerFromInstanceID(controllerState.id);
            SDL_GameControllerRumbleTriggers(controller, s1_rumble, s2_rumble, duration_ms);
        }
        break;
    }

    default: NODEFAULT;
    }
}
