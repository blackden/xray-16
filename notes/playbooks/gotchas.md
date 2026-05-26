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

**`git merge` НЕ дёргает `post-commit` — нужен `post-merge`.**
*Где:* `scripts/git-hooks/post-merge` — trampoline, форвардит на
`post-commit`. Установлено через `make install-hooks`.
*Симптом:* issue-driven merges в master не триггерят auto-ship, ship-логов
нет, бинарь в `.app` отстаёт от исходников. Если добавляешь новый hook —
прикинь, бывает ли он на «обоих» путях (commit + merge), иначе merge-only
flow его пропустит.

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

**Cached `CTheoraSurface` нужно rewind'ить при UI gap.**
*Где:* `Layers/xrRender/SH_Texture.cpp::apply_theora`,
`Layers/xrRenderGL/glSH_Texture.cpp::apply_theora` — gap-check на
`m_theora_last_apply_frame`; `pTheora->Rewind()` при >10 кадров без apply.
*Симптом:* интро/outro/любое OGM-видео при повторном открытии экрана
играет с произвольного кадра. Корень: `CTexture` живёт в глобальном
`m_textures`-кэше, `pTheora->playing` не сбрасывается между сессиями UI,
GL-бэкенд грузит OGM в looped-mode. Если правишь видео-поведение — не
лезь в `Play()`, для `<auto_static>`-backed видео он не вызывается;
только для `<item type="video">` (которых в `intro_game.xml` нет).

**PreCache: 20 frames на Apple, 60 на остальных.**
*Где:* `game_sv_single.cpp:344-357`.
*Симптом:* первое движение после load может dropp'нуть FPS на Apple —
это ожидаемо (см. decisions.md #12). Не «баг», не чини.

**`glFinish()` на Apple дешевле чем кажется.**
*Где:* `D3DXRenderBase.cpp:257`, called from `IGame_Persistent.cpp:455`.
*Симптом:* если боишься его использовать «потому что блокирующий» — на
Metal-backed GL он flush'ит command buffer и возвращается. Не full
pipeline stall как на NVIDIA driver.

**`SDL_SetWindowResizable(SDL_FALSE)` убивает Cocoa fullscreen Space eligibility.**
*Где:* SDL2 cocoa backend (`SDL_cocoawindow.m:2454-2476`); наши callers —
`Device_mode.cpp:143,148` (оба Apple-gated).
*Симптом:* при `resizable=FALSE` SDL зовёт `setCollectionBehavior:
NSWindowCollectionBehaviorFullScreenNone` — окно теряет право на
native fullscreen Space, `SDL_SetWindowFullscreen(DESKTOP)` падает в
borderless overlay fallback, Cmd-Tab minimize'ит в Dock вместо
переключения Spaces. Apple-gate любой `SDL_SetWindowResizable(SDL_FALSE)`
перед fullscreen entry на macOS — native fullscreen Space сам блокирует
resize, RESIZABLE-bit держать безопасно. См. #99.

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

**`tea` CLI должен быть авторизован на `git.fedorov.tech` (Gitea-host).**
*Где:* `scripts/issues/sync.sh`.
*Симптом:* `sync-issues` валится — нет логина под `$GITEA_HOST`.
Workaround: `tea login add --name fedorov --url https://git.fedorov.tech
--token <pat>`. PAT с scope'ами `read:repository, write:issue` создаётся
в `git.fedorov.tech/-/user/settings/applications`.
Примечание: в Claude-сессиях issues пушатся через Gitea MCP server
(`mcp__gitea__issue_write`); скрипт нужен только для cron / out-of-session
backfill'ов.

**Hang? Сверься с taxonomy ПРЕЖДЕ чем формулировать гипотезу.**
*Где:* [`notes/decisions/hang-taxonomy.md`](../decisions/hang-taxonomy.md).
*Симптом:* hard system freeze, `STAT=TX` zombie, Cmd+Q ∞-wait, watchdog
firing — для любого taxonomy сужает диагноз до Family 1.A / 1.C / 2.B
за 30 секунд. Без неё ты (или Claude через 2 недели) изобретёт N-ную
гипотезу для уже известного класса бага. И помни ключевое: STAT=TX это
**наша** вина (не отпустили GPU/IOKit context), не Apple-side bug.

---

## macOS networking / syscall semantics

> Уроки из #71 (updater main-thread hang) и cpp-engineer / apple-platform
> peer reviews 2026-05-23/24. Записаны чтобы не потратить ещё 8 часов на
> тот же класс ошибок.

**macOS `sample` tool attributes ticks по program counter, НЕ по wait-state.**
*Где:* `/usr/bin/sample` (Apple диагностический tool).
*Симптом:* hot loop, который тысячу раз в секунду вызывает syscall
(`send`/`recv`) и получает `EAGAIN`, в выводе `sample` выглядит
**идентично** треду, реально заблокированному в этом syscall — оба
показывают 100% времени в `libsystem_kernel.__sendto` (или похожем).
*Как различать:* `ps -o wchan=,pcpu,stat -p <pid>` — `wchan=-` + `pcpu≈100`
= spin loop; `wchan=<имя>` + `pcpu≈0` = реально заблокирован.
Альтернатива: `dtrace -n 'syscall::sendto:entry /pid==N/ { @[ustack()]=count(); }'`
покажет реальный rate вызовов.

**`shutdown(fd, SHUT_WR)` на Darwin НЕ генерирует POLLHUP локально.**
*Где:* любой код пытающийся cancel'нуть socket activity из другого треда
через half-shutdown.
*Симптом:* Linux на shutdown(SHUT_WR) генерирует POLLHUP в локальном
`select`/`poll` → ждущий тред просыпается. macOS — нет. `select`
продолжает возвращать тот же `writeFlag` (по состоянию kernel send
buffer). Если буфер был full перед shutdown, select навсегда вернёт
writeFlag=0, send никогда не вызовется, EPIPE никогда не surface'ится.
*Применение:* для cross-thread cancel'а ghttp/любого poll-based
networking — shutdown не работает. Нужен либо `close(fd)` (race vs
allocator/recycling), либо cooperative cancel flag, либо `kqueue +
EVFILT_USER` wake-up на dedicated worker.

**`SO_SNDTIMEO` / `SO_RCVTIMEO` silently игнорируются на non-blocking сокетах.**
*Где:* `setsockopt` после `fcntl(fd, F_SETFL, O_NONBLOCK)`.
*Симптом:* setsockopt возвращает 0 (успех), но таймауты не применяются.
`send`/`recv` ведут себя как non-blocking — EAGAIN мгновенно, no timeout.
POSIX semantics: SO_*TIMEO — таймаут на блокирующее ожидание; нет
блокировки → нет таймаута. На Linux то же самое.
*Применение:* для timeout'а non-blocking socket'а нужен либо userspace
deadline, либо `TCP_USER_TIMEOUT` — но **macOS TCP_USER_TIMEOUT не
имплементирует**, только Linux (RFC 5482).

**`TCP_KEEPALIVE` не fires против peer'а который ACKает на kernel
level но не reads userspace'ом.**
*Где:* классический test pattern `nc -l <port>` (TCP accept, no read).
*Симптом:* setsockopt SO_KEEPALIVE + агрессивные TCP_KEEPALIVE/INTVL/CNT
не дают ECONNRESET через ожидаемые ~5s. Connection остаётся «живой» с
точки зрения kernel'а навсегда.
*Почему:* Darwin `tcp_timer.c::TCPT_KEEP` ресетит `t_rcvtime` на любой
полученный segment — включая zero-window probe ACKs (RFC 1122
§4.2.2.17). Когда peer userspace не read'ит, recv buffer полнеет → peer
advertise'ит window=0 → sender уходит в persist state → отправляет
1-байтовые probes → peer's kernel ACKает probes без userspace
involvement → keepalive timer никогда не достигает idle threshold.
*Применение:* keepalive создан для **dead remotes** (нет ACKов), не для
**stuck userspace** (TCP стек живой, апп замёрз). Для второго класса —
только cooperative cancel или async-port архитектура.

**`ghttpCancelRequest` НЕ thread-safe; `ghttpCloseRequest` — да (только shutdown).**
*Где:* `Externals/GameSpy/src/GameSpy/ghttp/ghttpMain.c:1011-1051`.
*Симптом:* cancel из другого треда → UAF / double-free. ghiFreeConnection
не имеет синхронизации, рассчитан только на ghttp-owning thread (= main).
*Workaround:* `ghttpCloseRequest` делает только `shutdown(fd, SHUT_WR)`
+ single result-enum store — POSIX-safe cross-thread. Но (см. выше) на
macOS shutdown(SHUT_WR) inert — так что это рабочий cross-thread
primitive только для блокированных I/O путей, не для non-blocking spin.

**`pthread_kill(SIGUSR1)` только прерывает EINTR-able syscalls.**
*Где:* любая попытка вытащить тред из stuck kernel call.
*Симптом:* `select`/`poll`/`recvfrom`/`connect` возвращают EINTR — отлично.
`__sendto` mid-copyin (когда kernel копирует userspace data в kernel
buffer) — **uninterruptible**. SIGUSR1 queued, доставится только когда
copy завершится сам по себе. То же для некоторых Mach IPC.
*Применение:* SIGUSR1 wake в watchdog (issue #75) помогает для
большинства hang-паттернов но не для глубокого sendto. Не серебряная пуля.

**Static `std::deque<std::function<...>>` с FastDelegate-captured `this` живёт дольше owner'а.**
*Где:* `src/xrGameSpy/ghttp_worker_apple.mm` `g_completionQueue` (file-static
deque, drained by main thread on frame boundary).
*Симптом, если нарвался:* worker thread пушит completion record в deque,
main thread не успевает дренить ДО того как owner (`CMainMenu`) деструктируется
(typical race: Cmd+Q после initiated download). Когда деструктор drain'a
наконец-то срабатывает (or static destruction reaches the deque) — `invoke()`
дёргает FastDelegate с уже-dead `this` → UAF.
*Lesson:* в teardown path'е (`CGameSpy_HTTP::CleanUp`) **ОБЯЗАТЕЛЬНО**
discard pending records без invoke'а — `OpenXRay_GhttpDiscardPendingCompletions()`
swap'ит deque локально и destructed его без вызова `invoke`. `std::function`
destructors освобождают captures чисто (sizeof, ABI-compatible) — это safe;
проблема **именно** в `invoke()`, не в самом cleanup'е. Любой будущий
producer→consumer queue с FastDelegate/lambda-captured pointers нужен
parallel discard surface, не только drain.
*Контекст:* gitea #117 A.2, cpp-engineer audit Bug 1.

**Vendored ghttp `gethostbyname` блочит worker → `dispatch_sync(ghttpCleanup)` блочит main.**
*Где:* `src/utils/mp_gpprof_server/libraries/gamespy/ghttp/ghttpProcess.c:232`
(actual linked copy lives in `Externals/GameSpy/src/GameSpy/ghttp/`).
*Симптом, если нарвался:* пользователь жмёт Cmd+Q пока ghttp request
в середине DNS resolution. Worker thread в blocking `gethostbyname` (до ~30с
default libc timeout на Darwin). `CGameSpy_HTTP::CleanUp` делает
`dispatch_sync(g_workerQueue, ^{ ghttpCleanup(); })` — main thread ждёт
пока worker освободится → graceful Cmd+Q вместо <1s занимает до ~30s. 10s
graceful watchdog (`macos_cocoa_shim.mm:108-114`) SIGKILL'ает раньше —
process умирает, но без чистого teardown.
*Lesson:* любая блокирующая sync функция в vendored library, которую ты
обернул в dispatch worker, всё равно блочит main path при teardown через
`dispatch_sync` barrier. Real fix — submodule patch (`gethostbyname` →
`getaddrinfo` with bounded timeout). Без него watchdog SIGKILL — fallback,
который сейчас работает но скрывает proper shutdown.
*Контекст:* gitea #117 A.2, cpp-engineer audit Bug 6 (P2 deferred to A.2.1).

**`_exit()` поверх stuck kernel syscall → STAT=TX zombie.**
*Где:* `xrDebug::StartWatchdog` (`src/xrCore/xrDebug.cpp:752-787`).
*Симптом:* `ps -o stat` показывает `TX` (stopped during exit). `kill -9`
не помогает. Процесс жив до перезагрузки.
*Почему:* `_exit` инициирует teardown (close fds, dealloc, etc), но main
thread в kernel — не может дойти до своей user-space cleanup части,
kernel не может полностью reap'нуть. На pre-Tahoe macOS видимо просто
долго ждал, на Tahoe — навсегда.
*Workaround:* SIGUSR1 wake перед _exit (#75), либо external supervisor
с Mach `task_terminate` (см. #63, отвергнуто как too heavy для personal
fork).

**`SCM_RIGHTS` передаёт file descriptors, НЕ `mach_port_t`.**
*Где:* `sendmsg(socket, ...)` с control message.
*Симптом:* если кто-то предложит передавать Mach task port через Unix
domain socket с SCM_RIGHTS — это будет workaround на ошибку. Mach ports
требуют `mach_msg` с `MACH_MSG_PORT_DESCRIPTOR` поверх уже-shared Mach
channel, либо `bootstrap_register` через launchd, либо
`mach_ports_register` перед exec в parent-spawns-child pattern.
*Контекст:* возникло в discussion #63 supervisor pattern.

**Tahoe TX zombie может появиться независимо от user-space mitigation.**
*Где:* Dock Force Quit (Option+click → Force Quit) на macOS 26.x.
*Симптом:* процесс жив-здоров, нажимаешь Force Quit → процесс жив,
STAT=TX, wchan=`-`, pcpu=0. Даже Activity Monitor → Force Quit не
помогает.
*Почему:* Apple-side баг в kernel signal delivery на Tahoe. Не лечится
из userspace. См. issue #63.
*Workaround:* перезагрузка системы. SIGUSR1 wake в watchdog (#75) **не**
лечит этот класс — там watchdog не успевает fire'нуть.

---

## Rejected approaches (чтобы не reconsider через полгода)

**`SDL_CreateWindowFrom` для engine-owned NSWindow на macOS — rejected 2026-05-26 (gitea #114).**
*Где:* был кандидат в roadmap A.1 (`docs/superpowers/specs/2026-05-25-native-shell-roadmap.md`).
*Симптом, если попробуешь:* fullscreen-Space eligibility фиксируется в момент `SDL_CreateWindowFrom` —
это struct-level property, не hint-able post-factum. SDL2 после embed всё равно зовёт
`setCollectionBehavior:` на attached NSWindow (конфликт с pre-set bits). NSView/GL context ownership
становится гибридным — SDL не создаёт NSView внутри custom NSWindow корректно, resize/move event
routing неполный. Cmd-Tab → Space stickiness ломается даже сильнее чем сейчас.
*Почему rejected:* throwaway scaffolding — ту же работу придётся переделать в A.2/A.3 когда
`[NSApp run]` + NSEvent input сделают SDL pump ненужным. Engine-owned NSWindow создаётся напрямую,
без хрупкого embed-гибрида. A.1 redesigned как «observers-only extension `OpenXRayCocoaShim`».
*Подтверждения сообщества:* SDL bug #2561 (window ownership), SDL bug #8518 (collectionBehavior race),
discourse.libsdl.org/t/27142.

---

## Что НЕ покрыто но возможно стоит добавить

- Как разрезать DMG на 2GB chunks для распространения (текущий 3.5GB).
- Notarization process — пока skip, signed с ad-hoc cert.
- Crash report extraction из `~/Library/Logs/DiagnosticReports/`.
