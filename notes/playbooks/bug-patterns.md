# Bug patterns — OpenXRay macOS

Каталог семейств багов, которые мы реально ловили в `macos/blackden/master`.
Цель — увидеть симптом, узнать семью за 30 секунд. Каждое семейство:
**Symptom / Root-cause shape / Examples / Fix template / Recognition cue**.

Связано: [architecture.md](architecture.md) — где живут описанные подсистемы.

---

## 1. Windows-ANSI assumption leak

**Symptom.** Любая операция с cyrillic путём / именем падает с EILSEQ
(`fopen`), либо возвращает мусор. На Windows ту же сборку всё работает.

**Root-cause shape.** Код написан в эпоху когда «строка» = cp1251 в
ANSI-локали Windows. На macOS APFS strict-UTF-8 — байты `0xC8 0xEB` (cp1251
«Ил») не валидны как UTF-8 и kernel отказывается принимать имя.
Эквивалентно для `readdir`, `glob`, `realpath`.

**Examples.**
- Autosave с cyrillic именем сейва → EILSEQ при `fopen`.
  Fix: `xrCore/FS_internal.h:79 retry_fopen_after_cp1251` — на EILSEQ
  транскодируем и пробуем снова.
- `Core.UserName` из `pw_name` ASCII-only был костылём; восстановлен
  `pw_gecos` (UTF-8) после Phase 3 (xrCore.cpp:443).
- NPC names в save: cp1251 байты в `m_character_name` от vanilla CoP
  save. Fix: post-load UTF-8 валидация + transcode в
  `xrServer_Objects_ALife_Monsters.cpp:256-261` и
  `InventoryOwner.cpp:185-190`.

**Fix template.**
1. Найди boundary (где байты переходят POSIX-syscall).
2. Convert at the boundary, not at callsite — callsites потом
   умножаются.
3. Gate behind `g_r__legacy_encoding` если файл/сейв с диска (vanilla
   compat).
4. Validate UTF-8 first (`xr_is_valid_utf8`) — если уже UTF-8, не
   ломай.

**Recognition cue.**
- `errno == EILSEQ` после `fopen` / `stat`.
- В log «??? ??? ???» на месте кириллицы.
- В save list пустое поле / mojibake вместо имени.
- Test case: создай user/save/level с именем «Илья» — если хоть один
  call site падает, это эта семья.

---

## 2. State-machine cache leak

**Symptom.** На N-ый кадр сцена начинает быстро падать в GL error
(0x502 INVALID_OPERATION storm), либо физика «прыгает», либо звук
играется не с того места. На холодном старте всё хорошо.

**Root-cause shape.** Объект кэширует часть upstream-состояния
(`cache.X = caller.X`), но при изменении другой части upstream
(`caller.Y`), которая косвенно делает `cache.X` невалидным, кэш не
сбрасывается. Через какое-то время cache != reality, downstream
читает stale значение.

**Examples.**
- `glR_Backend_Runtime.h::set_Format` (line 146): при смене декларации
  меняется VAO bind; новый VAO **не содержит** `glVertexBuffer` binding.
  Но cache хранил `vb = old_vb` и `set_Vertices` (line 250) решал «vb
  уже привязан» и пропускал bind. Результат: `glDrawElements` на VAO
  без вершинного буфера → 0x502 storm. Fix: после `glBindVertexArray`
  тоже сбрасывать `vb = 0; vb_stride = 0` (line 167-168).
- LocatorAPI `m_files` index не invalidate'ился при `rescan_path` — Settings
  пропадали после save+restart. Fix: `_set_root` теперь зовёт rescan.

**Fix template.**
1. Найди cache в setter (`if (cache.X == new_X) return;`).
2. Enumerate ВСЕ downstream state'ы которые зависят от X и других
   полей, которые меняются вместе с X (даже косвенно).
3. Каждый dependent inval'ься явно в setter, не надейся на «потом
   догонит».
4. Optional: rename cache field в `last_<bound>_<X>` чтобы было
   очевидно «это кэш bound state, не value to use».

**Recognition cue.**
- Симптом проявляется после N кадров / переключения сцены / pause.
- На pause сцена работает, после resume — рассыпается.
- В debug build с `force_invalidate_cache` (если есть) симптом
  пропадает.
- В git blame setter — последняя правка добавляла новое поле без
  инвалидации старого.

---

## 3. Path-separator mismatch (Windows backslash vs POSIX)

**Symptom.** POSIX syscall (`stat`, `unlink`, `open`) возвращает ENOENT
для пути, который Engine считает существующим. Логи показывают путь с
`\` separator'ами.

**Root-cause shape.** Движок исторически работал с Windows-путями, и
большая часть кода хранит / составляет пути с `\`. На POSIX
переводчик слеша только частичный — `LocatorAPI` транслирует на boundary,
но если код напрямую составляет path через `string_concat` или после
`FS.update_path`, ему нужен явный `convert_path_separators`.

**Examples.**
- Safe-mode sentinel `stat()` падал с ENOENT потому что путь приходил
  как `\Users\ragnar\.openxray-data\_appdata_\.boot_in_progress`.
  POSIX `stat` не принимает backslash как separator на не-Windows.
  Fix: `x_ray.cpp:480 convert_path_separators(sentinel)` перед stat.
- Settings/saves пропадали после restart — `FS_Path::_set_root` не
  нормализовал separator при rescan. Fix в LocatorAPI.

**Fix template.**
1. Если новый код составляет path и зовёт POSIX call — `convert_path_separators(buf)`
   перед call.
2. Если получаешь path из `FS.update_path` / `m_Path` — он Windows-style,
   convert before POSIX.
3. Output для логов — можно оставить как есть (читается на любой ОС).

**Recognition cue.**
- `stat`/`open`/`unlink` returns -1, `errno == ENOENT` для пути
  который явно существует.
- В лог-output путь содержит `\`.
- File explorer показывает файл, но движок его «не видит».

---

## 4. GL state untracked across binds

**Symptom.** GL_INVALID_OPERATION на каждом frame, иногда сразу,
иногда после первого scene-switch. Часто spam в console.

**Root-cause shape.** Specific случай семьи (2), но достаточно частая
на GL core profile чтобы выделить. В core profile многие state'ы
**прикреплены к VAO** (attribute pointers, element array binding).
`glBindVertexArray` атомарно swap'ает весь набор. Engine cache думает
что «buffer X привязан», но на новом VAO он не привязан.

**Examples.**
- 0x502 storm на Apple GL 4.1 (см. семью 2 выше).
- IB cache leak in `glR_Backend_Runtime.h::set_Indices` (если будет
  similar bug в будущем — invariant выше его поймает).

**Fix template.**
1. После `glBindVertexArray(X)` — invalidate всю кэш-state'у,
   относящуюся к attribute pointers и element array binding.
2. В debug build можно glGet'нуть текущий VAO state и assert'ить
   против кэша.
3. Помни: VAO в core profile = «весь pipeline state кроме программы и
   uniforms».

**Recognition cue.**
- `GL_INVALID_OPERATION` (0x502) после first draw call следующего scene.
- Logs show repeated 0x502 storm после mode change (windowed →
  fullscreen, settings reload).
- `apitrace` / RenderDoc показывает что `glBindBuffer(GL_ARRAY_BUFFER)`
  не вызывается перед `glVertexAttribPointer` — но cache думает что
  вызвался.

---

## 5. Lua/C++ string boundary unannounced

**Symptom.** Какое-то конкретное поле UI / save показывает «???» или
mojibake, при этом соседние поля нормальные. Разница — данное поле
приходит из Lua script.

**Root-cause shape.** В X-Ray Lua interop возвращает `LPCSTR` —
сырые байты, без encoding tag. Lua скрипты часто пишут в cp1251
(вся vanilla CoP gamedata). C++ caller, после Phase 1 UTF-8 migration,
ожидает UTF-8. Boundary не помечен, transcode не делается.

**Examples.**
- `m_character_name` приходит из Lua-script через
  `xrServer_Objects_ALife_Monsters` save/load chain. Fix: post-load
  UTF-8 validate + cp1251 transcode (line 256-261).
- `m_game_name` тоже из Lua через trader scripts. Fix:
  `InventoryOwner.cpp:185-190`.

**Fix template.**
1. Найди Lua-boundary (`luabind::call_member`, `lua_State::tostring`,
   `script_object::name()`).
2. После load — `if (!xr_is_valid_utf8(s.c_str())) { xr_cp1251_to_utf8(...);
   s = buf; }` шим.
3. Gate behind `g_r__legacy_encoding`.
4. Долгосрочно: помечать каждый Lua-возвращаемый string в Engine типом
   `xr_legacy_string` — но это огромный рефактор, отложен.

**Recognition cue.**
- Одно поле «???», соседние OK.
- Поле populated в Lua script (`*.script` файл в gamedata).
- Логировать первые 16 байт перед UI display — если 0xC0-0xFF без
  валидного UTF-8 continuation, это cp1251.

---

## 6. Cleanup hangs because GPU busy (TX-state)

**Symptom.** Cmd+Q (или alt+f4 / другой clean shutdown) на macOS
зависает в beachball на 30+ секунд. В Activity Monitor процесс в
TX-state. Kill -9 работает мгновенно, но это не shutdown.

**Root-cause shape.** Engine cleanup делает `glDelete*` (textures,
buffers, programs) синхронно в main thread. На Metal-backed GL это
serializes через `mach_msg` — GPU должна закончить in-flight commands
прежде чем driver сможет освободить ресурсы. Если GPU занят (shadow
cascades в queue, occlusion queries pending, terrain streaming), это
ожидание длится секунды-минуты и в kernel выглядит как TX-state.

**Examples.**
- Cmd+Q **в главном меню** — работает мгновенно: GPU idle.
- Cmd+Q **в level** — зависает: shadow cascades + occlusion + streaming
  queue не выгрузились.

**Fix template.**
1. (Best) Drain GPU queue перед cleanup: `glFinish()` + wait for
   occlusion queries + sync with streaming thread.
2. (Pragmatic) `_exit(0)` из Cocoa Cmd+Q shim — kernel освободит всё
   само, никакого engine cleanup. Минус: skip user-level autosave если
   игрок не сохранился. Плюс: всегда работает, без TX-state.
3. (Long-term) async cleanup в отдельном thread с timeout — если
   shutdown > 5s, force-exit.

**Recognition cue.**
- Процесс в TX-state в Activity Monitor.
- Console: последняя строка — какой-нибудь `glDelete*` или
  `IRender::Destroy()`.
- На main menu shutdown быстрый, в level — медленный.

---

## Что бы я хотел добавить в этот доку дальше

- **Window-mode race conditions** — мы их пока не словили, но они тут
  watching. SDL2 + ResizeEvent + active frame in flight — рисковано.
- **Streaming-thread vs main-thread synchronization** — `prefetch_tick`
  делает `glFinish` потому что иначе race с texture upload. Это
  отдельный паттерн, но у нас пока одна точка.
- **Lua coroutine teardown** — теоретически source багов в save/load
  cycle, не словил пока.

Когда поймаю — расширю.
