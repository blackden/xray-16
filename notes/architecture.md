# Architecture map — OpenXRay (macOS fork)

Личная справка по проекту. Цель — отвечать на «где живёт X» за 30 секунд,
а не за 30 минут grep'а. Только то, что я переоткрывал больше одного раза.

Дата: 2026-05-19. Stable: `0e712a51b` (`macos/blackden/master`).
Связанные доки: [bug-patterns.md](bug-patterns.md), [decisions.md](decisions.md),
[gotchas.md](gotchas.md), [engine-thoughts.md](engine-thoughts.md).

## Module dependency DAG

Стрелка `A → B` = «A зависит от B».

```
                xr_3da (entry point, ~30 строк)
                   │
                   ▼
                xrEngine ◄────────── Layers/xrRender* ──► xrAPI
                   │                  (R2/R4/GL)              ▲
                   │                                          │
        ┌──────────┼──────────────────────────────┐           │
        ▼          ▼            ▼                 ▼           │
     xrGame    xrSound     xrParticles      xrCDB ──► xrPhysics
        │          │            │                 │           │
        ▼          ▼            ▼                 ▼           │
   xrAICore   xrUICore   xrServerEntities   xrMaterialSystem  │
        │          │            │                 │           │
        └─────┬────┴────────────┴─────────────────┘           │
              ▼                                               │
           xrCore  ◄──── xrScriptEngine ◄──── xrGameSpy       │
              │                                               │
              ▼                                               │
           Common (header-only platform shims) ───────────────┘
```

Циклов нет — DAG honest. `Layers/xrRender*` плагины: только реализуют
`IRender_Interface` из `xrAPI`, не зависят друг от друга. Активный
рендерер выбирается рантаймом в `xr_3da/entry_point.cpp::s_render_modules`.

## Where does X live

Таблица «нужная подсистема → стартовый файл». Для долгих файлов даю file:line.

| Что | Где | Заметка |
|---|---|---|
| Application entry | `xr_3da/entry_point.cpp` | создаёт CApplication, передаёт renderer-модули |
| Game loop | `src/xrEngine/x_ray.cpp::CApplication::Run` | главный while-цикл |
| Device tick | `src/xrEngine/Device.cpp::CRenderDevice::on_idle` | per-frame pump |
| File system | `src/xrCore/LocatorAPI.cpp`, `src/xrCore/FS_internal.h` | overlay paths, archives |
| `-overlaypath` flag | `LocatorAPI.cpp:1041-1055` | `$app_data_root$` rewrite |
| Path index | `LocatorAPI::m_files` | hash-map всех известных файлов |
| FS path separators | `convert_path_separators(buf)` | engine paths back-slashed → POSIX нужен forward |
| Cp1251 retry на write | `FS_internal.h::retry_fopen_after_cp1251` (line 79) | APFS EILSEQ recovery |
| Encoding helpers | `src/xrCore/xrCore.cpp:34-+` | `xr_legacy_to_utf8`, `xr_cp1251_to_utf8`, `xr_is_valid_utf8` |
| Encoding console vars | `src/xrCore/xrCore.h:166-180` | `g_r__trace_encoding`, `g_r__legacy_encoding` |
| Console var registration | `src/xrGame/console_commands.cpp:2244-2249` | `r__trace_encoding`, `r__legacy_encoding` |
| UTF-8 primitives | `src/xrCore/Text/StringConversion.hpp` | `xr_decode_utf8`, `xr_codepoint`, `xr_utf8_is_continuation`, `xr_utf8_lead_size` |
| Text classifier predicates | `StringConversion.hpp:60-78` | `IsAlphaCharacter`, `IsNeedSpaceCharacter`, etc — accept `xr_codepoint` |
| Font renderer | `src/xrEngine/GameFont.cpp/h` | `s_utf8_mode = true` (line 195), `SlotForCodepoint`, `m_codepointToSlot` |
| Glyph imprint | `src/Layers/xrRender/dxFontRender.cpp::ImprintCharCp` | per-codepoint vertex emit |
| UI line layout | `src/xrUICore/Lines/UILines.cpp::ParseText` (line 73) | word-wrap, s_utf8_mode branch at 175 |
| Line editor | `src/xrEngine/line_edit_control.cpp` | `on_text_input` (368), `move_pos_left/right` (~620) |
| Key bindings → name | `src/xrEngine/xr_input.cpp::KbdKeyToButtonName` (line 520) | `SDL_GetScancodeName` — physical, layout-independent |
| XML loader | `src/xrCore/XML/XMLDocument.cpp::Load` (line 163), `SetWithEncodingShim` (196) | sniffs `encoding=` attribute |
| INI loader | `src/xrCore/xr_ini.cpp::CInifile::Load` | per-line cp1251→UTF-8 if invalid UTF-8 |
| Save/load (alife) | `src/xrGame/alife_storage_manager.cpp:91-220` | autosave write path |
| NPC names from save | `src/xrServerEntities/xrServer_Objects_ALife_Monsters.cpp:251-261` | cp1251→UTF-8 shim post-load |
| Game-name shim | `src/xrGame/InventoryOwner.cpp:181-190` | cp1251→UTF-8 shim post-load |
| Save toast | `src/xrGame/console_commands.cpp:706` | UTF-8 passthrough now |
| ImGui font | `src/xrEngine/Device_imgui.cpp:64-65` | `/System/Library/Fonts/Supplemental/Arial.ttf` |
| Clipboard | `src/xrCore/os_clipboard.cpp` | UTF-8 byte-passthrough |
| Username | `src/xrCore/xrCore.cpp:433-449` | `pw_gecos` first, `pw_name` fallback |
| Lua bridge | `src/xrScriptEngine/` | luabind exports |
| Sound engine | `src/xrSound/` | OpenAL backend |
| AI/pathfinding | `src/xrAICore/` | planner, navmesh |
| Collision | `src/xrCDB/` | OPCODE-based |
| Physics | `src/xrPhysics/` | ODE-based |
| Particles | `src/xrParticles/` | runtime emitter |
| Renderer abstract | `src/Layers/xrAPI/` | `IRender_Interface` |
| Renderer GL | `src/Layers/xrRenderGL/`, `src/Layers/xrRenderPC_GL/` | Apple/Linux default |
| GL backend cache | `src/Layers/xrRenderGL/glR_Backend_Runtime.h::CBackend::set_Format` (line 146) | VAO + decl + vb/vb_stride |
| `FlushGpuQueue` | `src/xrEngine/Render.h:386` (virtual), `src/Layers/xrRender/D3DXRenderBase.cpp:257` (glFinish) | called from `IGame_Persistent.cpp:455` |
| Prefetch loop | `src/xrEngine/IGame_Persistent.cpp::Prefetch_Tick` | calls FlushGpuQueue at end |
| Apple PreCache | `src/xrGame/game_sv_single.cpp:344-357` | `PRECACHE_FRAMES = 20` on Apple, 60 elsewhere |

## Invariants

Release-blocking истины. Если нарушить — что-то ломается, иногда не сразу.

1. **`Config.hpp` first include** — каждый `.cpp` начинается с `#include "Config.hpp"`
   (либо сразу после PCH). Macro-related crashes если нарушить.
2. **`s_utf8_mode = true` — THE switch.** Любой font path должен уважать обе
   ветки (legacy single-byte и UTF-8 codepoint). См. `GameFont.cpp:195`,
   `dxFontRender.cpp` (s_utf8_mode branch), `UILines.cpp:175`.
3. **`r__legacy_encoding` console var управляет cp1251 read-shims** в
   `XMLDocument.cpp` и `xr_ini.cpp`. Default ON (vanilla compatibility).
   Off — только если у тебя гарантированно UTF-8 gamedata.
4. **`convert_path_separators(buf)` before POSIX call.** Engine хранит пути
   с backslash, POSIX `stat()`/`fopen()`/`unlink()` их не принимают. См.
   `x_ray.cpp:480` для канонического примера (sentinel cleanup).
5. **Safe-mode sentinel путь.** Launcher пишет `${APPDATA_DIR}/_appdata_/.boot_in_progress`,
   engine читает `$app_data_root$/.boot_in_progress` — резолвится в ту же
   точку потому что `LocatorAPI` префиксует `_appdata_/` к
   `$app_data_root$`. Любое изменение префикса требует синхронизации
   `package_app.sh:235` и `x_ray.cpp::Run` cleanup loop.
6. **Apple GL VAO bind инвалидирует attribute pointers.**
   `glVertexAttribPointer` пишет в currently-bound VAO. Поэтому в
   `set_Format` после `glBindVertexArray(new_vao)` мы должны сбросить
   также `vb`/`vb_stride` cache — иначе `set_Vertices` решит «vb уже
   привязан» и пропустит binding. Это причина 0x502 storm которую мы
   ловили в Phase 4. См. `glR_Backend_Runtime.h:146-168`.
7. **Save format binary, position-tied, версии не имеет.** Добавить поле в
   середину save struct = ломает все существующие сейвы. Шим cp1251→UTF-8
   в `InventoryOwner.cpp:185-190` и `xrServer_Objects_ALife_Monsters.cpp:256-261`
   работает потому что строки сохраняются как `length+bytes`, можно
   валидировать post-load.

## Encoding boundary map

Где текст входит в движок и в какой кодировке.

```
┌────────────────────────────────────────────────────────────────────┐
│                       UTF-8 GUARANTEED                              │
│   SDL_TEXTINPUT  ─►  line_edit_control::on_text_input               │
│   fopen() POSIX  ─►  byte-transparent (macOS APFS требует UTF-8)    │
│   clipboard      ─►  os_clipboard.cpp (UTF-8 passthrough)           │
│   Mac username   ─►  xrCore.cpp pw_gecos                            │
│   ImGui          ─►  Device_imgui.cpp (Arial.ttf + Cyrillic range)  │
└────────────────────────────────────────────────────────────────────┘
                                │
                                ▼
                       ┌──────────────────┐
                       │  ENGINE INTERNAL │
                       │     (UTF-8)      │
                       └──────────────────┘
                                ▲
                                │  (transcode on entry)
┌────────────────────────────────────────────────────────────────────┐
│                  CP1251 + SHIM (gated by g_r__legacy_encoding)      │
│   XML loader     ─►  XMLDocument::SetWithEncodingShim               │
│   INI loader     ─►  CInifile::Load (per-line UTF-8 validate)       │
│   Save load_data ─►  per-field shim в InventoryOwner +              │
│                      xrServer_Objects_ALife_Monsters                │
│   Lua return     ─►  callsite responsibility (см. NPC names)        │
└────────────────────────────────────────────────────────────────────┘
                                ▲
                                │  (validate, может содержать что угодно)
┌────────────────────────────────────────────────────────────────────┐
│                          NEEDS VALIDATION                           │
│   Command line   ─►  Core.Params                                    │
│   ltx fsgame     ─►  paths in fsgame.ltx                            │
│   modder mods    ─►  unknown encoding, often cp1251                 │
└────────────────────────────────────────────────────────────────────┘
```

Output side — всё пишется как UTF-8 нативно через `CFileWriter` (POSIX
fopen — byte-transparent). Если cp1251 leaked через какой-то путь и
APFS отверг — `FS_internal.h::retry_fopen_after_cp1251` (line 79)
транскодирует и повторяет один раз.

## Build & promote flow

```
   commit на macos/blackden/master
        │
        ▼
   post-commit hook (scripts/git-hooks/post-commit)
        │
        ├── docs-only? ──► skip
        ├── stable branch? ──► skip
        ├── OPENXRAY_AUTO_SHIP=0? ──► skip
        ▼
   make ship (background)  ─► /Applications/OpenXRay-Dev.app
        │
        │ (user testing, "всё ок")
        ▼
   make promote (manual)
        │
        ├── fast-forward STABLE_WORKTREE
        └── install into STABLE_APP_DIR ─► /Applications/OpenXRay.app
```

Ключевые цели в `Makefile`:
- `ship` (line 320) — `make install INSTALL_APP_DIR=$DEV_APP_DIR`
- `promote` (line 351) — fast-forward stable worktree + install
- `install-hooks` (323) — symlink scripts/git-hooks/* в .git/hooks/
- `sync-issues` (345) — gh-based comment/close для `Closes #N` в commits

Пути:
- Dev .app: `/Applications/OpenXRay-Dev.app` (`DEV_APP_DIR`)
- Stable .app: `/Applications/OpenXRay.app` (`STABLE_APP_DIR`)
- Stable worktree: `STABLE_WORKTREE` (отдельная папка)
- Auto-ship log: `$HOME/Library/Logs/OpenXRay/ship-${sha}.log`

## Apple GL 4.1 gotchas

macOS Big Sur+ предоставляет OpenGL **только в core-profile 4.1**, реализован
поверх Metal. Из этого следует:

- **Нет `ARB_vertex_attrib_binding`** (он часть GL 4.3+). Атрибуты
  bind'ятся per-format через VAO, не через global state.
- **VAO must be bound** перед любой attribute операцией. У нас в
  `set_Format` есть VAO-per-format cache.
- **`glDelete*` во время активного frame** часто блокируется на mach_msg
  (TX-state в Activity Monitor). Особенно в момент shutdown когда GPU
  ещё обрабатывает shadow cascades / occlusion queries. См.
  bug-patterns.md «Cleanup hangs because GPU busy».
- **`glFinish()` дешевле чем кажется** на Metal-backed GL — оно
  flush'ит command buffer и возвращается. Используется в
  `D3DXRenderBase.cpp:257` для async prefetch barrier.
- **PreCache 60 frames слишком много** — на Apple первый минутный пик
  GPU нагрузки приводит к TX-state. `game_sv_single.cpp:344-357` режет
  до 20 frames на Apple. Тёплый старт без stutter.

## Что не покрыто здесь

- Renderer internals (shadow cascades, occlusion query, terrain LOD) —
  ridge не разобрано. В TODO если/когда полезем в renderer playground
  (см. `engine-thoughts.md` dream-project #2).
- Networking / multiplayer (`xrNetServer`, `xrGameSpy`) — singleplayer
  фокус, не лез.
- Lua scripting layer details — поверхностно, дойдут руки когда полезем
  в моддинг.
- Editor tools (`xrLC_Light`, `xrCompress`) — Windows-only, не лез.
