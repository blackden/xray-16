# Gotchas — OpenXRay (macOS fork)

Короткий список «потеряешь час, если не знаешь». Формат:

> **Gotcha.** Краткое описание.
> *Где:* file:line.
> *Симптом, если нарвался:* что увидишь.

Связано: [architecture.md](architecture.md), [bug-patterns.md](bug-patterns.md),
[decisions.md](decisions.md).

---

## Code-level

**`Config.hpp` должен быть первым `#include` в любом `.cpp`.**
*Где:* конвенция, см. CLAUDE.md.
*Симптом:* macro-related compile errors на ровном месте, типа `XRCORE_API`
undefined.

**Engine paths используют `\` separator; POSIX syscalls — `/`.**
*Где:* `xrCore/LocatorAPI` хранит paths с backslash; `convert_path_separators(buf)`
переводит в forward slash перед POSIX call.
*Симптом:* `stat()`/`open()`/`unlink()` returns -1 с ENOENT для пути
который явно существует. В логе путь содержит `\`. Канонический
пример: `x_ray.cpp:480`.

**`s_utf8_mode = true` — оба font path branches respect it.**
*Где:* `GameFont.cpp:195`, `dxFontRender.cpp` (s_utf8_mode branch),
`UILines.cpp:175`.
*Симптом:* если правишь font/text path и не учёл branch — UTF-8 строки
рендерятся как single-byte mojibake или vice versa.

**`xr_codepoint` (u32) vs `xr_wide_char` (u16) — predicate widening.**
*Где:* `StringConversion.hpp:10`.
*Симптом:* старый код сравнивает с `0xFFFF` как «invalid» — теперь
валидный codepoint. Implicit promotion компилируется без warning.

**`R_ASSERT` no-op в ReleaseMasterGold.**
*Где:* `xrCore/_std_extensions.h`.
*Симптом:* assertion-based проверка в release silently игнорится. Для
production-critical checks используй `Msg("! ...") + early return`, не
`R_ASSERT`.

**Save format binary, position-tied, без version field.**
*Где:* `xrServer_Objects_*.cpp` save/load methods.
*Симптом:* добавил поле в середину save struct — все существующие сейвы
ломаются. Добавляй только в конец, или сделай versioned migrator (его
ещё нет).

**Lua strings crossing boundary могут быть cp1251.**
*Где:* `xrScriptEngine` boundary; конкретные callers: `m_character_name`
(xrServer_Objects_ALife_Monsters.cpp:256-261), `m_game_name`
(InventoryOwner.cpp:185-190).
*Симптом:* одно поле UI «???», соседние нормальные. Fix-шаблон: post-load
`xr_is_valid_utf8` → `xr_cp1251_to_utf8` если invalid.

**`r__legacy_encoding` console var ON by default.**
*Где:* `xrCore.cpp:35`, console var в `console_commands.cpp:2249`.
*Симптом:* XML/INI silently транскодируются с cp1251. Если хочешь
тестировать pure-UTF-8 path — `r__legacy_encoding 0` в console.

**`xr_codepoint` 0xFFFD (U+FFFD REPLACEMENT) — fallback при битом UTF-8.**
*Где:* `StringConversion.hpp:15`, `xr_decode_utf8`.
*Симптом:* если видишь `�` в логе — где-то upstream UTF-8 валидация
не прошла, проверяй boundary.

---

## Build / Distribution

**`make ship` ≠ `make promote`.**
*Где:* `Makefile:316-357`.
*Симптом:* ожидал что `make ship` обновит `/Applications/OpenXRay.app` —
а он обновляет `OpenXRay-Dev.app`. Stable дergaется только `make promote`.

**Auto-ship hook фильтрует docs-only commits.**
*Где:* `scripts/git-hooks/post-commit`.
*Симптом:* поправил только `notes/*.md` — никакого notification, билд
не запускается. Это by design (см. decisions.md #7).

**Auto-ship hook работает только на `macos/blackden/master` и `macos/dev`.**
*Где:* `scripts/git-hooks/post-commit` case-statement.
*Симптом:* коммит на feature branch — hook молчит. Если нужно сшипить с
feature-branch — `make ship` руками.

**Auto-ship лог: `$HOME/Library/Logs/OpenXRay/ship-${sha}.log`.**
*Где:* `scripts/git-hooks/post-commit`.
*Симптом:* билд провалился, нужно понять что — открой этот файл.

**`openxray.log` накапливается до гигабайтов (#24 pending).**
*Где:* `${APPDATA_DIR}/_appdata_/openxray.log`.
*Симптом:* DMG/zip stale-large, disk full warning. Workaround сейчас:
`rm "${APPDATA_DIR}/_appdata_/openxray.log"`. Long-term fix: log rotation
at startup (task #24).

**DMG volume name с пробелами ломает `-fsltx` parser.**
*Где:* `scripts/mac/package_app.sh` — DMG packaging. Engine parser
использует `sscanf("%[^ ] ", ...)`.
*Симптом:* `/Volumes/OpenXRay All-in-One/...` → engine видит
`/Volumes/OpenXRay`. Workaround: называть volume `OpenXRay-AllInOne`
(без пробелов).

---

## Runtime

**Apple GL 4.1 core profile — no `ARB_vertex_attrib_binding`.**
*Где:* `Layers/xrRenderGL/`.
*Симптом:* код пишущий через `glVertexBuffer`/`glBindVertexBuffer` style
не скомпилируется на macOS. Используй `glVertexAttribPointer` с
bound VAO.

**VAO bind инвалидирует attribute pointers — cache vb/vb_stride надо
сбрасывать.**
*Где:* `glR_Backend_Runtime.h:146-168` (`set_Format`).
*Симптом:* 0x502 INVALID_OPERATION storm после N кадров. Если новый код
зовёт `glBindVertexArray` — invalid'ируй render-state cache.

**Cmd+Q в level → TX-state hang.**
*Где:* engine cleanup `glDelete*` блокируется mach_msg при busy GPU.
*Симптом:* процесс зависает в beachball после Cmd+Q. Workaround
runtime: kill -9. Code-level fix pending: либо `_exit(0)` из Cocoa
shim, либо drain GPU queue before delete.

**Cmd+Q в main menu — работает мгновенно.**
*Где:* same as above.
*Симптом:* нет симптома, это сравнение для диагностики. Если TX-state
случается **и** в main menu — значит другая проблема.

**Safe-mode sentinel: `${APPDATA_DIR}/_appdata_/.boot_in_progress`.**
*Где:* `package_app.sh:235`, `x_ray.cpp:474-495`.
*Симптом:* hand-debug safe-mode — проверь что sentinel реально
существует/удалён. Не путай с APPDATA_DIR root (без `_appdata_/`) —
прошлый bug.

**PreCache: 20 frames на Apple, 60 на остальных.**
*Где:* `game_sv_single.cpp:344-357`.
*Симптом:* первое движение после load может dropp'нуть FPS на Apple —
это ожидаемо (см. decisions.md #12). Не «баг», не чини.

**`glFinish()` на Apple дешевле чем кажется.**
*Где:* `D3DXRenderBase.cpp:257`, called from `IGame_Persistent.cpp:455`.
*Симптом:* если боишься его использовать «потому что блокирующий» — на
Metal-backed GL он flush'ит command buffer и возвращается. Не full
pipeline stall как на NVIDIA driver.

---

## Process

**Stable promote требует ручного approve.**
*Где:* `Makefile::promote`.
*Симптом:* зашипил билд, играется хорошо — а в `/Applications/OpenXRay.app`
старый. Нужно явно `make promote` после user-validation.

**`OPENXRAY_AUTO_SHIP=0` отключает auto-ship для текущей сессии.**
*Где:* `scripts/git-hooks/post-commit:19`.
*Симптом:* WIP-коммит не должен генерировать ship-noise — exporting
`OPENXRAY_AUTO_SHIP=0` перед коммитом.

**`gh` CLI должен быть `gh auth login` на target repo (blackden/xray-16).**
*Где:* `scripts/issues/sync.sh`.
*Симптом:* `sync-issues` валится — у `gh` нет credentials. Workaround:
`gh auth login` вручную в свой fork.

---

## Что НЕ покрыто но возможно стоит добавить

- Как разрезать DMG на 2GB chunks для распространения (текущий 3.5GB).
- Notarization process — пока skip, signed с ad-hoc cert.
- Crash report extraction из `~/Library/Logs/DiagnosticReports/`.
