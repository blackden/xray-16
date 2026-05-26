#include "stdafx.h"
#pragma hdrstop

#include "xr_input.h"
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
} // namespace
#endif

#if defined(XR_PLATFORM_APPLE)
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
};

void VerifyInputTable()
{
    int total = 0;
    int matches = 0;
    int mismatches = 0;
    int unmapped = 0;

    for (int keyCode = 0; keyCode < 128; ++keyCode)
    {
        SDL_Scancode ourScancode = kNSKeyCodeToSDLScancode[keyCode];

        // SDL doesn't expose its NSEvent keyCode table directly. Approximate
        // check: SDL_GetKeyFromScancode(ourScancode) should return a valid
        // key (i.e. ourScancode is real). For unmapped entries, ourScancode
        // is SDL_SCANCODE_UNKNOWN (0).
        if (ourScancode == SDL_SCANCODE_UNKNOWN)
        {
            ++unmapped;
            continue;
        }

        SDL_Keycode key = SDL_GetKeyFromScancode(ourScancode);
        if (key == SDLK_UNKNOWN)
        {
            Msg("! verify_input_table: keyCode 0x%02x -> scancode %d -> no SDL key",
                keyCode, ourScancode);
            ++mismatches;
        }
        else
        {
            ++matches;
        }
        ++total;
    }

    Msg("verify_input_table: total=%d matches=%d mismatches=%d unmapped=%d",
        total + unmapped, matches, mismatches, unmapped);
}
} // namespace

extern "C" void OpenXRay_VerifyInputTable()
{
    VerifyInputTable();
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

    bool mouseMoved = false;
    int offs[2]{};
    float scroll[2]{};
    const auto mousePrev = mouseState;
    mouseAxisState[2] = 0;
    mouseAxisState[3] = 0;

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

    for (int i = 0; i < MOUSE_COUNT; ++i)
    {
        if (mouseState[i] && mousePrev[i])
            cbStack.back()->IR_OnMouseHold(IdxToKey[i]);
    }

    if (mouseMoved)
    {
        if (offs[0] || offs[1])
            cbStack.back()->IR_OnMouseMove(offs[0], offs[1]);

        if (!fis_zero(scroll[0]) || !fis_zero(scroll[1]))
            cbStack.back()->IR_OnMouseWheel(scroll[0], scroll[1]);
    }
}

void CInput::KeyUpdate()
{
    ZoneScoped;

    SDL_Event events[MAX_KEYBOARD_EVENTS];
    const auto count = SDL_PeepEvents(events, MAX_KEYBOARD_EVENTS,
        SDL_GETEVENT, SDL_KEYDOWN, SDL_KEYMAPCHANGED);

    // Let iGetAsyncKeyState work correctly during this frame immediately
    for (int i = 0; i < count; ++i)
    {
        const SDL_Event& event = events[i];

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
        SDL_SetRelativeMouseMode(grab ? SDL_TRUE : SDL_FALSE);

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
        SDL_StartTextInput();

    SDL_PumpEvents();
    SDL_FlushEvents(SDL_TEXTEDITING, SDL_TEXTINPUT);
}

void CInput::DisableTextInput()
{
    --textInputCounter;
    if (textInputCounter < 0)
        textInputCounter = 0;

    if (textInputCounter == 0)
        SDL_StopTextInput();

    SDL_PumpEvents();
    SDL_FlushEvents(SDL_TEXTEDITING, SDL_TEXTINPUT);
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
