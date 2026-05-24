# UTF-8 migration — manual smoke-test checklist

Used after each commit that touches text rendering, FS encoding, or
localization. Pair with `make test-encoding` (automated half).

## Baseline (current production behavior, pre-migration)

These run on the `macos/stable` bundle in `/Applications/OpenXRay.app`. The
current expectations are what we want to PRESERVE through Phase 0 and break
deliberately as Phase 1 ships.

| # | Action | Expected today | After Phase 1 |
|---|---|---|---|
| B1 | Launch, observe main menu | Renders correctly | Identical |
| B2 | Start new game → "Начало игры" autosave triggers | File appears in `~/Games/STALKER-CoP/_appdata_/savedgames/` as `ragnar - начало игры.scop` (UTF-8 on disk) | Identical |
| B3 | Open Load Game menu | Save list shows mojibake for cyrillic names (`��������`) | Renders cyrillic correctly |
| B4 | Click autosave → load | Loads successfully | Identical |
| B5 | F5 quicksave | `ragnar - quicksave.scop` lands on disk | Identical |
| B6 | Change resolution to max → apply | Window clamps to usable bounds (e.g. 1680×939) | Identical |
| B7 | Open in-game console (`~`) | Console opens, English text input works | Identical |
| B8 | In console, type cyrillic via SDL_TEXTINPUT (Russian keyboard layout) | Characters lost / replaced with `?` | Cyrillic renders correctly, cursor advances per codepoint |

## Phase 0 smoke-tests (test harness only)

These are the green-flag for Phase 0. Run `make test-encoding` — all should pass.

- [ ] `utf8_validator_test`: 35/35
- [ ] `cp1251_roundtrip_test`: 13/13
- [ ] `mb_decode_test`: 45/45
- [ ] fixture `phrase.utf8` is valid UTF-8
- [ ] fixture `phrase.cp1251` is NOT valid UTF-8 (confirms it's actual cp1251)
- [ ] `make test` (full suite incl. safe_append + regression) green

## Phase 1 exit-criteria smoke-tests (renderer accepts UTF-8)

Trigger only after `s_utf8_mode = true` lands (final step of Phase 1).

- [ ] HUD English text "EXIT" / "INVENTORY" renders identical to baseline
- [ ] HUD cyrillic localization ("ВЫХОД", "ИНВЕНТАРЬ") renders correctly
      (today: mojibake; after Phase 1 should render glyphs)
- [ ] Console: type "Привет" via Russian keyboard layout, all 6 chars visible
- [ ] Console: backspace once after typing "Привет" — last codepoint disappears
      cleanly, not just 1 byte (which would leave a half-codepoint)
- [ ] Console: left-arrow once, then delete — middle codepoint removed, no
      stray continuation byte
- [ ] Load Game menu: autosaves with cyrillic event names render correctly
- [ ] Save dialog: type cyrillic name, confirm save, file lands on disk with
      UTF-8 bytes matching what was displayed
- [ ] No new GL errors in `~/Library/Logs/OpenXRay/openxray.log`
      (regression: font path shouldn't introduce 0x502)

## Phase 2 exit-criteria (XML transcoding shim)

- [ ] In `~/.openxray-data/_appdata_/user.ltx`, set `language=rus` — main menu
      strings render in cyrillic without mojibake
- [ ] Same for `language=ukr` (Ukrainian)
- [ ] Same for `language=pol` (Polish, uses cp1250 not cp1251)
- [ ] `language=eng` (ASCII) unchanged
- [ ] With `r__trace_encoding 1` in console: log shows transcoded files exactly
      once per first load, not on every reload
- [ ] No new "Couldn't parse XML" errors in engine log

## Phase 3 exit-criteria (write hardening + macOS cleanup)

- [ ] Set macOS account display name to "Илья" — engine `Core.UserName` picks
      up cyrillic UTF-8 from pw_gecos
- [ ] Save file shows `Илья - quicksave.scop` on disk
- [ ] Load Game menu displays "Илья - quicksave" without mojibake
- [ ] Remove our retry workaround in alife_storage_manager → autosaves still
      persist (proves CFileWriter retry took over)
- [ ] ImGui debug overlay (`dbg_draw_actor_phys`) — any cyrillic debug string
      renders correctly

## Phase 4 exit-criteria (native UTF-8 resources)

- [ ] Localization XMLs in `res/gamedata/configs/text/rus/` are valid UTF-8
      per `iconv -f UTF-8 -t UTF-8`
- [ ] `make build` runs the CMake UTF-8 validator and passes
- [ ] Engine loads with shim disabled (`r__legacy_encoding 0`) and cyrillic UI
      works
- [ ] Engine still loads vanilla CoP gamedata with `-legacy_encoding` flag
      (vanilla gamedata.db archives carry cp1251 XMLs that we don't touch)
- [ ] Final `grep -rn 'xr_utf8_to_cp1251' src/` — only declaration + 1-2
      legacy-compat callers remain, no fresh callers

## Failure modes to watch for (any phase)

| Symptom | Likely cause | Where to look |
|---|---|---|
| Mojibake in formerly-fine menus | Renderer/shim mismatch — XML transcoded but font still byte-indexed | `CGameFont::SizeOf_`, `s_utf8_mode` flag |
| Save not on disk after gameplay | CFileWriter EILSEQ retry regressed | `~/Library/Logs/OpenXRay/openxray.log` for `! Can't write file: ... EILSEQ` |
| Crash on cyrillic console input | `line_edit_control` cursor math wrong | grep `m_cur_pos` in line_edit_control.cpp |
| Localization broken for one locale | Phase 2 shim doesn't detect encoding for that file | `r__trace_encoding 1` |
| GL 0x502 storm on game start | Font code mis-binds VAO/EBO during glyph render | `CHW::CreateDevice` diagnostic line |
| Window opens at wrong size | viewport clamp interacted with new code | `* SelectResolution: ...` log line |

## Recovery from broken dev bundle

If `/Applications/OpenXRay-Dev.app` is unusable, the stable bundle at
`/Applications/OpenXRay.app` is independent (built from `macos/stable`
worktree at `../xray-16-stable`). Play from the stable bundle, then:

```bash
cd ~/fedorov_tech/xray-16
git log --oneline -10                    # find last good dev commit
git reset --hard <last-good-sha>         # discard broken
INSTALL_APP_DIR=/Applications/OpenXRay-Dev.app make install
```

The stable bundle stays untouched.
