# A.7.2 — Native NSTextInputContext replaces SDL text-input pathway

**Date:** 2026-05-28
**Roadmap step:** A.7.2 (native-shell-roadmap §4)
**Issue:** [#165](https://git.fedorov.tech/ragnar/xray-16/issues/165)
**Status:** ✓ Closed.

## Decision

On macOS the engine owns an `NSTextInputContext` directly via a new
`CNativeTextInputBackend` (`src/xrEngine/NativeTextInputBackend.mm`).
The SDL `SDL_StartTextInput` / `SDL_TEXTINPUT` pathway is gone from
the Apple build — replaced end-to-end:

- `CInput` factory selects `CreateNativeTextInputBackend()` under
  `XR_PLATFORM_APPLE`, `CreateSDLTextInputBackend()` everywhere else.
- The A.3 NSEvent local monitor in `macos_cocoa_shim.mm` hands KeyDown
  to `OpenXRay_HandleNativeTextInputKeyDown` (which drives
  `[NSTextInputContext handleEvent:]` → `insertText:` →
  `IR_OnTextInput`). Non-committed keystrokes (Enter, arrows, Escape,
  function keys, dead-key mid-composition) fall through to the A.3
  ring path so navigation in text surfaces still works.
- `CInput::KeyUpdate` no longer runs `SDL_PeepEvents` on Apple at all.
  The whole drain block is gated by `#if !defined(XR_PLATFORM_APPLE)`.

## Why

The SDL_TEXTINPUT pathway was the last remaining reason the A.3
NSEvent local monitor had to selectively let keyDown events through
to SDL (the "text-input gate"). That pass-through created a parallel
ingest race: AppKit dispatches keyDown to both the local-monitor
block AND the `[NSWindow keyDown:]` responder chain, so SDL received
keystrokes in parallel even when our monitor swallowed them. The
result was a multi-PR cascade of double-dispatch and phantom-key bugs
(#155, #156, #157, #158, #159, #160, #162). Owning the input context
ourselves makes the monitor authoritative for every keystroke
regardless of mode: gameplay → A.3 ring → `IR_OnKeyboard*`; text
surfaces → native context → `IR_OnTextInput`. SDL's parallel ingest
still happens, but with no consumer of its output the SDL queue
becomes inert — it accumulates and is never read.

## Trade-off — committed text only

`CNativeTextInputBackend` implements the minimum `NSTextInputClient`
surface needed for committed UTF-8 delivery. Marked-text (IME
composition preview) returns sentinel values everywhere:
`hasMarkedText → NO`, `markedRange → {NSNotFound, 0}`,
`attributedSubstringForProposedRange: → nil`, etc.

What works:
- ASCII / Latin precomposed input.
- Cyrillic precomposed input (RU PC, RU ISO Mac).
- Emoji-picker commit (the picker shows itself, user picks, commit
  arrives via `insertText:`).
- Dead-key composition that ends in commit (the user sees no preview
  but the final committed character arrives).

What does NOT work:
- Visible IME composition preview ("marked text" feedback — Japanese
  / Chinese / Korean composing inline; dead-key composition indicator
  in the input field).

Acceptable for CoP today: all text surfaces in the engine (console,
save-name dialog, multiplayer chat, ImGui InputText overlays) want
committed text only. No consumer renders marked text — and the
adversarial review of A.6 documented this explicitly
(`notes/decisions/a6-textinput-contract.md`: "Ни один consumer не
подписан на SDL_TEXTEDITING (marked text). Все хотят committed UTF-8
only.").

## Revisit if

- A future surface needs visible IME composition (CJK player input in
  multiplayer chat, dead-key indicator in save-name dialog). That
  requires implementing the full marked-text protocol AND rendering
  the marked range in the consumer widget — a real refactor of
  `line_edit_control` + ImGui InputText integration.

## What dies naturally

By dropping the SDL_TEXTINPUT pathway:

- **SDL drain loop on Apple** (`xr_input.cpp:692-794`) — entire
  `SDL_PeepEvents` block now under `#if !defined(XR_PLATFORM_APPLE)`.
  No more `s_sdlDrainWasSkipping` state, no more parallel-ingest
  guard.
- **Flush-on-resume hack** (`SDL_FlushEvents(SDL_KEYDOWN,
  SDL_KEYMAPCHANGED)` on drain skip → resume transition) — gone with
  the drain.
- **NONUSBACKSLASH alias in drain branch** (former #162 mitigation in
  the SDL drain entry) — gone with the drain. The A.3 ring path's
  own `kVK_ANSI_Grave → SCANCODE_GRAVE` mapping in
  `xr_input.cpp:kNSKeyCodeToSDLScancode` is the only path that maps
  this physical key now, so the divergence class is structurally
  closed.
- **`SDL_StopTextInput()`** unconditional call in `x_ray.cpp:263`
  removed. macOS-only fork (per memory `feedback_macos_fork_only`) —
  no need to leave it for hypothetical Linux/Win builds; whoever
  takes the code cross-platform will see the absence and add it back.
- **`SDL_ResetKeyboard()`** workaround in
  `OpenXRay_NotifyTextInputActive(0)` — was a #155 (Bug 7) mitigation
  for SDL phantom-held key state across gate toggles. With no SDL
  drain on Apple, SDL's `keyboardState` is no longer consulted by
  the engine; the workaround is moot.

## NONUSBACKSLASH in 3 files outside xr_input.cpp — kept

Three out-of-scope files mention `SDL_SCANCODE_NONUSBACKSLASH`:

| File | Line | Context | Decision |
|------|------|---------|----------|
| `xr_level_controller.cpp` | 328 | `{ "kNONUSBACKSLASH", SDL_SCANCODE_NONUSBACKSLASH, "Non US Backslash" }` in the full keybind name table | **leave** |
| `key_binding_registrator_script.cpp` | 328 | `value("DIK_NONUSBACKSLASH", int(SDL_SCANCODE_NONUSBACKSLASH))` luabind export of the DIK constant | **leave** |
| `editor_helper.h` | 200 | `//case SDL_SCANCODE_NONUSBACKSLASH: return ImGuiKey_;` (commented out in scancode → ImGui key map) | **leave** |

All three predate the ISO Mac landmine and bind the legitimate SDL
scancode constant for ANSI keyboards and non-Mac systems where the
key exists as a distinct physical button. None of them is part of
the #162 ISO Mac workaround; stripping them would remove a legit
binding constant.

The actual #162 mitigation was the alias inside the SDL drain branch
(`xr_input.cpp` ~line 745-770 pre-A.7.2). That alias is gone because
the entire branch it lived in is gone.

## Codified

- `notes/decisions/a6-textinput-contract.md` — pointer added.
- `notes/reference/engine-map.md` — Apple gotchas section updated:
  NSTextInputContext ownership recorded as the new authoritative
  text-input path; SDL parallel ingest reclassified from "must gate
  around" to "inert (no consumer)".

## References

- A.6 contract: `notes/decisions/a6-textinput-contract.md`
- Roadmap §4: `docs/superpowers/specs/2026-05-25-native-shell-roadmap.md`
- Apple Cocoa Event Handling Guide — "Text Input"
- Apple NSTextInputClient protocol reference
