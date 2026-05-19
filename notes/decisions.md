# Decisions log — OpenXRay (macOS fork)

Запись «почему мы сделали так, а не иначе». Compaction съедает rationale
первым; этот лог его переживает. Формат каждого пункта:

> **Decision** — что сделали.
> **Why** — мотивация (часто конкретный incident).
> **Trade-off** — что мы при этом потеряли.
> **Revisit if** — какое условие должно измениться чтобы переоценить.

Связанные: [architecture.md](architecture.md), [bug-patterns.md](bug-patterns.md),
[gotchas.md](gotchas.md).

---

## 1. `s_utf8_mode` = `static bool`, не console var

**Decision.** В `GameFont.cpp:195` константа `static bool CGameFont::s_utf8_mode = true`.
Не registered как ConsoleVar.

**Why.** UTF-8 — это режим работы движка, не runtime-preference. Менять его
на лету бессмысленно: font atlas строится один раз при load, codepoint
mapping тоже. Console var создавала бы иллюзию toggle'абильности.

**Trade-off.** Нельзя A/B-тестировать legacy vs UTF-8 рендер на ходу. Если
понадобится — recompile.

**Revisit if.** Окажется что часть моддерского контента (custom fonts с
non-standard ranges) ломается под UTF-8 mode — тогда возможно сделать
per-font флаг, но не global toggle.

---

## 2. EILSEQ retry в `CFileWriter` ctor, не в callsite'ах

**Decision.** Шим `xr_cp1251_to_utf8` + retry живёт в `xrCore/FS_internal.h:52-79`
прямо в file writer constructor. Никаких per-callsite retry в
`alife_storage_manager`, `console_commands` и т.д.

**Why.** Изначально я делал retry в `alife_storage_manager` (autosave path)
— это работало, но через 2 дня я нашёл такую же проблему в `save_data`
у NPC names и ещё в console snapshot. Каждый callsite пришлось бы патчить
отдельно. FS-layer покрывает все callers разом.

**Trade-off.** Менее explicit — call site не видит что мог быть retry.
Logged через `Msg("! EILSEQ retry on %s")` так что diagnostic есть.

**Revisit if.** Окажется что retry дорог (он не дорог — fopen + iconv) или
маскирует баги в callsites (тогда лог-вывод + assert в debug build).

---

## 3. cp1251 patches в `load_data` (per-field), не versioned save migrator

**Decision.** Шим UTF-8 validate + cp1251 transcode стоит в конкретных
полях после `load_data`: `m_character_name` (xrServer_Objects_ALife_Monsters.cpp:256-261),
`m_game_name` (InventoryOwner.cpp:185-190).

**Why.** Vanilla CoP save format не имеет version field в начале. Менять
формат значит ломать совместимость с миллионом сейвов модового
community. Per-field validate — минимально достаточный fix.

**Trade-off.** Каждое новое UTF-8-relevant поле в save потребует копию
этого шима. Если их будет > 5, имеет смысл вынести в helper
`normalize_legacy_string(s)`.

**Revisit if.** Появится versioned save migrator (например, для какого-то
другого breaking change). Тогда внести encoding migration туда как
versioned step.

---

## 4. Sentinel path = `${APPDATA_DIR}/_appdata_/.boot_in_progress`

**Decision.** Safe-mode sentinel живёт в `_appdata_/` subdirectory под
APPDATA, launcher и engine используют одинаковый путь. Engine
резолвит через `FS.update_path("$app_data_root$", ".boot_in_progress")`,
что после `LocatorAPI` префиксации даёт ту же точку.

**Why.** Первая версия (commit `1b85d75ef`) положила sentinel рядом с
APPDATA_DIR (без `_appdata_/`), engine же читал из
`$app_data_root$` который **включает** `_appdata_/` префикс — пути не
совпадали, sentinel «никогда не существовал» в глазах engine, safe-mode
не активировался.

**Trade-off.** `_appdata_/` — внутренняя деталь LocatorAPI, теперь
зашита в launcher script. Если LocatorAPI поменяет префикс — launcher
script сломается тихо.

**Revisit if.** LocatorAPI меняет конвенцию `$app_data_root$` resolution.
Или захотим вынести sentinel вообще из user-visible APPDATA (например,
в `/private/tmp`).

---

## 5. Safe-mode clears at 120 frames, не at quit

**Decision.** Engine удаляет sentinel после **120 стабильных кадров**
(`x_ray.cpp:399 STABLE_BOOT_FRAMES = 120`), а не при `Application::Quit`.

**Why.** Cmd+Q + TX-state hang оставляет процесс в живых на минуты,
никакой Quit-callback не успевает отработать (см. bug-patterns.md
семья 6). 120 frames = ~2 секунды стабильного игрового loop'а; этого
достаточно чтобы признать «boot прошёл».

**Trade-off.** Если игрок крашится между frame 120 и Cmd+Q, sentinel
уже сброшен — safe-mode не активируется. Acceptable: основной кейс
который мы хотим поймать — crash в Initialize, который случается до 120.

**Revisit if.** Увидим crashes около frame 100-150 которые safe-mode
должен был бы поймать, но не поймал. Тогда поднять STABLE_BOOT_FRAMES
до 300-600.

---

## 6. Stable promote — manual `make promote`, не auto

**Decision.** Auto-ship hook пушит билды только в `OpenXRay-Dev.app`.
Перенос в stable требует ручного `make promote`.

**Why.** Stable = user-validated. Зелёный билд ≠ играющийся билд. Я
несколько раз попадал в ситуацию когда build OK, но в игре регрессия —
авто-promote удалил бы рабочий stable.

**Trade-off.** Дополнительный manual step. Окупается тем что stable
действительно стабилен — есть к чему откатиться.

**Revisit if.** Добавим автоматизированный smoke-test который запускает
xr_3da headless, грузит сейв, проверяет N кадров без crash. Тогда
можно поставить auto-promote-after-smoke-test.

---

## 7. Auto-ship hook skips docs-only commits

**Decision.** `scripts/git-hooks/post-commit` фильтрует diff и skip'ает
если все файлы в `notes/`, `*.md`, `.claude-tmp/`, `.gitignore`,
`.git-blame-ignore-revs`, `tests/regression_checks.sh`.

**Why.** Docs-only не меняют binary. Rebuild = 5-10 минут CPU + диск +
osascript notification. Зачем.

**Trade-off.** Если случайно отредактируешь `Makefile` и зайдёт под
docs-фильтр — auto-ship всё равно сработает. Список paths узкий
конкретный, не glob — false-positive shouldn't happen.

**Revisit if.** Захотим автоматизированно билдить docs в HTML / site —
тогда docs-only коммит должен триггерить doc-build, а не engine-build.

---

## 8. Treat as personal macOS fork; ignore upstream OpenXRay

**Decision.** Не делаем PR'ов в `OpenXRay/xray-16`. Не следим за
upstream `dev` branch. Не пытаемся сохранить совместимость с
Windows-side patches.

**Why.** Upstream OpenXRay поддерживает Windows + Linux + macOS как
peers. Каждый patch нуждается в cross-platform proof. Мы — solo
developer на macOS, и наши приоритеты другие: быстрый macOS-ship,
playable build, не upstream-ready code review process.

**Trade-off.** Через год upstream может уйти далеко вперёд, merge
станет невозможен. Мы согласны — это **personal** fork, не
contribution.

**Revisit if.** Решим что хочется внести что-то в upstream (например,
0x502 fix полезен для всех GL users). Тогда делаем cherry-pick из
наших коммитов в clean PR-branch.

---

## 9. `r__legacy_encoding` defaults ON (vanilla compatibility)

**Decision.** `g_r__legacy_encoding = 1` по умолчанию (xrCore.cpp:35).
XML/INI read-shims активны.

**Why.** Vanilla CoP `gamedata` cp1251. Default-off означал бы что
out-of-box vanilla install показывает «???» для всей локализации.
Это худший first-impression.

**Trade-off.** Скан per-file `xr_is_valid_utf8` стоит ~0.1ms на
файл; для тысяч файлов gamedata это копится в десятки ms на boot.
Acceptable.

**Revisit if.** Полностью перейдём на UTF-8 gamedata bundling (всё
переконвертировано) — тогда default off, легаси оставим как opt-in
для модов.

---

## 10. ImGui Cyrillic via system Arial.ttf, не embedded font

**Decision.** `Device_imgui.cpp:64` использует
`/System/Library/Fonts/Supplemental/Arial.ttf` напрямую.

**Why.** macOS гарантированно поставляет Arial. Не нужно bundle'ить
font в DMG (+1MB). Качество приемлемое для debug overlay.

**Trade-off.** Tied к Apple, на Linux так не сработает. Apple-only код
уже под `#ifdef XR_PLATFORM_APPLE`.

**Revisit if.** Apple удалит Arial (вряд ли). Или захотим красивый
font для production UI (не debug) — тогда bundle DejaVu/Noto.

---

## 11. `glFinish` at end of prefetch tick (Apple-only) — TX-state mitigation

**Decision.** `IGame_Persistent.cpp:455` зовёт `GEnv.Render->FlushGpuQueue()`
в конце prefetch loop. На Apple это `glFinish()`
(`D3DXRenderBase.cpp:257`), на других платформах no-op
(`Render.h:386`).

**Why.** На Apple GL prefetch tick наполнял GPU command queue быстрее
чем GPU успевал обрабатывать. К концу prefetch'а GPU задолжен 200+
frames worth of work, в момент перехода к Run() это вызывало
TX-state на 5-10 секунд.

**Trade-off.** `glFinish` это full GPU barrier — теряем ~16ms на final
prefetch frame. Не заметно, prefetch и так несколько секунд.

**Revisit if.** Появится way to flush без полного barrier (например,
GL_NV_query_buffer на NVIDIA — у Apple нет). Или появятся data что
glFinish стоит больше 16ms.

---

## 12. macOS-only `Device.PreCache(20)` instead of 60

**Decision.** `game_sv_single.cpp:357 PRECACHE_FRAMES = 20` на Apple,
60 на остальных.

**Why.** 60 frames PreCache spinning camera через все directions создаёт
~60 frames worth of shadow cascade rebuild + texture streaming + LOD
generation. Apple GL queue не успевает, попадает в TX-state.

**Trade-off.** На Apple первое движение после load может dropp'нуть FPS
на 1-2 sec пока недогенерированные ассеты допекутся. Acceptable:
лучше короткий stutter в gameplay чем 60-second TX-state на load.

**Revisit if.** Async streaming будет работать без блокировки main
thread (отдельный thread для texture upload). Тогда можно вернуть 60.

---

## 13. `xr_codepoint` = `u32` рядом с `xr_wide_char` = `u16`

**Decision.** В `StringConversion.hpp:10` добавили `typedef u32 xr_codepoint`
рядом с существующим `xr_wide_char` (`u16`). Predicate-функции
(`IsAlphaCharacter`, etc) теперь принимают `xr_codepoint`.

**Why.** Старый `xr_wide_char = u16` ограничен BMP (U+0000..U+FFFF) —
не вмещает emoji, экзотические скрипты. UTF-8 decoder возвращает
полный codepoint вплоть до U+10FFFF, который нужен u32.

**Trade-off.** Старые callsite'ы с `xr_wide_char` получают implicit
promotion (`u16 → u32`), что компилируется без warning'а. Если кто-то
сравнивает с `0xFFFF` как «invalid», теперь это не работает (валидный
codepoint). Не нашли таких сравнений в codebase, но possible regression
если появятся.

**Revisit if.** Появится strong typing (`enum class Codepoint : u32`)
которое можно постепенно ввести. Сейчас typedef проще.

---

## Что хочу добавить в будущем

- Решение по Apple-side keychain / security framework, если полезем в
  GameSpy auth.
- Решение по C++17 vs C++20 — пока следуем upstream (C++17), но
  `std::span` / `std::format` упростили бы кучу мест.
- Решение по Vulkan-via-MoltenVK timeline (если возьмёмся за dream-project #1
  из engine-thoughts.md).
