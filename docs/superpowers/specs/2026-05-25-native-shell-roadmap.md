# Roadmap: native macOS shell

Дата: 2026-05-25
Эпик: [#87](https://git.fedorov.tech/ragnar/xray-16/issues/87)
Тип: roadmap-документ (не technical spec)
Brainstorm origin: gitea [#106](https://git.fedorov.tech/ragnar/xray-16/issues/106)

## 1. Контекст

После двух недель интенсивных Apple-фиксов в SDL (с 2026-05-14, 272+
коммитов) накопился architectural ceiling, который точечными правками не
пробивается:

- **#99** (Cmd-Tab Space stickiness) — три rounds попыток в SDL-слое
  (PR #102 убрать `BORDERLESS` at creation, PR #103 Apple-gate
  `SetWindowResizable`, PR #104 .mm shim с KVO на
  `collectionBehavior`) показали что eligibility-бит правильный, но
  native Cocoa Space всё равно не engaged. SDL-обёртка вокруг
  NSWindow не переживает Space transitions чисто. **Реально лечится
  только сменой ownership.**
- **Family 1 hangs** из [`notes/decisions/hang-taxonomy.md`](../../../notes/decisions/hang-taxonomy.md)
  (sendto blocking, ghttp polling, GPU TX state) — структурно не
  лечатся пока главный цикл = SDL pump поверх AppKit. Любой blocking
  syscall на main thread → SDL pump не работает → WindowServer не
  сервится → system-wide freeze. Единственный structural fix —
  NSRunLoop как single source of truth.
- **Native UX** (меню-бар, About, Cmd+,, Preferences pattern) —
  невозможен без собственного NSApplicationDelegate. Сейчас delegate
  принадлежит SDL.

Это не три бага, а один architectural ceiling. Native shell —
единственный structural fix.

## 2. Решения, принятые в этом roadmap

| Вопрос | Решение |
|---|---|
| Декомпозиция эпика | Три направления: **A** окно+runloop, **B** native UX-полишинг, **C** подпись и доставка |
| Порядок | **A → B → C** |
| Горизонт | 3-6 месяцев, темп «не спешу, качественно» |
| Глубина A | **A3** — полное выбрасывание SDL с macOS |
| Подход к A3 | Инкрементальная замена подсистем (7 последовательных issue+PR в `macos/blackden/master`); не теневой билд и не feature-ветка с big-bang |

### Почему такой порядок

- **A первое** очевидно — закрывает #99, лечит Family 1, даёт
  фундамент для всего остального. Полировать меню до того как окно
  нормально работает — бессмысленно.
- **B перед C** — ты primary tester, поэтому B даёт ежедневное
  качество жизни именно тебе. C нужен только когда дойдёт до
  публичной раздачи за пределы VPN-канала — это позже.

### Почему инкрементальный подход, а не другие

Альтернативы рассматривались:

- **Подход 2 — теневой нативный билд** (`xr_3da_native` параллельно с
  SDL'ной версией). Отвергнут: дублирование `entry_point.cpp`, две
  цепочки инициализации, двойной CI, головняк «куда заливать
  багфикс».
- **Подход 3 — feature-ветка с big-bang** (`feature/native-shell` на
  2-3 месяца, один мега-PR). Отвергнут: нарушает issue-driven
  workflow, нет промежуточных гейтов, накапливаемые регрессии
  вываливаются разом в конце, мерж-конфликты с интеграционной
  веткой нарастают.

«Что-то работающее всегда под рукой» при инкрементальном подходе
обеспечивается базово: каждый шаг — PR в `macos/blackden/master`,
проходит smoke и auto-ship. После каждого шага бинарь работает в
реальной игре. Плюс stable release track (`make promote`,
`/Applications/OpenXRay.app`) — отдельный страховочный канал на
откат.

## 3. Направление A — окно + runloop (детально)

Каждый шаг — отдельный issue в gitea, per-issue branch, PR в
`macos/blackden/master`. Можно остановиться на любом шаге — бинарь
работает. Прогресс через гейты, не через сроки.

| # | Шаг | Что делает | Гейт «готово» |
|---|---|---|---|
| **A.1** | Свой NSWindow + NSApplicationDelegate | Engine создаёт NSWindow сам (через `apple-platform` shim в `.mm`), не SDL. NSApplicationDelegate перехватывает Cmd+Q, sleep/wake, applicationDidBecomeActive. SDL продолжает пампить события поверх существующего окна (новый mode «embed-into-given-NSWindow»). | #99 Cmd-Tab остаётся в своём Space; sleep/wake пауза рендера; 30 мин CoP без regression |
| **A.2** | NSRunLoop + CADisplayLink как главный цикл | Главный цикл становится event-driven (`[NSApp run]`), а не `while(!quit){ pump; tick; render; }`. CADisplayLink триггерит render tick (vsync-aligned). Любые blocking операции принудительно уходят в `dispatch_queue` или `NSURLSession`. | Family 1.A: PR #76 reproducer (nc-stall :8000) больше не вешает систему; ghttp polling не блочит main thread |
| **A.3** | NSEvent input pipeline | Клавиатура, мышь, scroll, modifier flags идут через NSEvent (не SDL_PollEvent). Маппинг в существующий `xrInput` слой. SDL остаётся как build-time зависимость, но code paths не дёргаются. | Все input scenarios CoP работают; layout switching (eng/rus) не теряет ввод; mouse capture/release корректный |
| **A.4** | Native timing | `SDL_GetTicks` / `SDL_GetPerformanceCounter` → `mach_absolute_time` / `CFAbsoluteTimeGetCurrent`. | Microbench: frame timing идентичен в пределах jitter; no drift на 1h soak |
| **A.5** | Audio audit | xrSound уже использует OpenAL напрямую, **не** через SDL_audio. Этот шаг — формально подтвердить и убедиться что нет случайных SDL_audio в инициализации. Скорее всего no-op. | grep `SDL_audio` / `SDL_OpenAudioDevice` в коде — пусто на macOS gate |
| **A.6** | Зачистка остатков | Clipboard (NSPasteboard), system info (`sysctlbyname`), file dialogs (NSOpenPanel где надо), любые остальные `SDL_*` calls под `#ifdef XR_PLATFORM_APPLE`. | grep `SDL_` в `src/xrEngine/`, `src/xrCore/` под Apple-gate — пусто |
| **A.7** | SDL2 уходит из macOS-билда | `find_package(SDL2)` обёрнут в `if(NOT APPLE)`, линковка `xr_3da` не тащит libSDL2. Bundle перестаёт включать SDL2.framework. | `otool -L bin/arm64/ReleaseMasterGold/xr_3da` не содержит SDL2; CI matrix зелёная; полный CoP playthrough |

**A.1 scope revision 2026-05-26 (gitea #114):** «Engine owns NSWindow + SDL embed» отложено до A.2 или позже. A.1 уменьшен до
observers-only extension `OpenXRayCocoaShim`: NSWorkspace sleep/wake + NSApplicationDelegate
`applicationDidBecomeActive:` / `applicationWillResignActive:`, applied at frame boundary через atomic flag в
`Engine.cpp` (`OpenXRay_ApplyPendingLifecycleEvent` в `CRenderDevice::ProcessFrame`). Обоснование: гейт #99 уже зелёный после PR #102/103, а
`SDL_CreateWindowFrom` на macOS имеет hybrid-ownership баги (SDL #2561, #8518; см.
`notes/playbooks/gotchas.md`). Engine-owned NSWindow появится в правильный момент — когда A.3 NSEvent input + A.2 NSRunLoop сделают
SDL pump ненужным, а не как промежуточный гибрид.

**Ключевая особенность подхода:** шаги A.1 → A.6 — это период когда
в коде живут оба механизма (SDL + native рядом). Это **временное
состояние одного объекта в процессе перехода**, не вторая сущность
навсегда. На каждом шаге SDL получает следующую «отрезанную руку»;
пока шаг не завершён, обе руки на месте и игра работает.

## 4. Направления B и C (контурно)

B и C получат собственные брейнштормы по факту подхода —
детализировать сейчас рано (требования могут измениться пока A идёт).
Здесь — только границы и порядок внутри.

### B — Native UX-полишинг

База: чеклист [`notes/reference/macos-native.md`](../../../notes/reference/macos-native.md)
(фазы 1-5, без 6 sandbox). После A.7 у нас уже свой
NSApplicationDelegate и NSWindow — поэтому B даже становится
**проще**, чем сейчас планируется в чеклисте: половина «App
lifecycle» уже сделана внутри A.

Приблизительный порядок шагов:

- B.1 — Native menu bar (Application / File / Edit / View / Window / Help)
- B.2 — About dialog (`NSStandardAboutPanelOptions` с SHA + version)
- B.3 — Preferences pattern (Cmd+,) — открывает engine options
- B.4 — HiDPI / Retina финальная вылизка (backing scale factor на 1x/2x/3x)
- B.5 — Стандартные shortcuts (Cmd+H, Cmd+M, Cmd+W)
- B.6 — Dock icon click — focus на existing window
- B.7 — Restorable state (Mission Control / Spaces — корректные snapshots)
- B.8 — VoiceOver не блокируется, accessibility basics

**Гейт «B done»:** `macos-native.md` фазы 1-2 + базовые элементы фаз
3 и 5 — все галки.

### C — Подпись и доставка

- C.1 — Developer ID Application certificate в keychain + entitlements
  (минимум: `cs.disable-library-validation` для LuaJIT, `cs.allow-jit`)
- C.2 — Hardened Runtime включён, `codesign --options runtime`
- C.3 — `notarytool submit` + `stapler staple`, `spctl -a -v` принимает
- C.4 — Signed DMG (`codesign --sign ... dist/OpenXRay-AllInOne.dmg`)
- C.5 — Sparkle (или эквивалент) — auto-update channel вне VPN
- C.6 — Решение по rename продукта (открытый вопрос из issue #87
  section 3) — финализируется здесь, не в A или B

**Гейт «C done»:** `macos-native.md` фаза 4 — все галки, плюс
публичная раздача DMG возможна и работает на чистом Mac'е без
right-click→Open.

## 5. Гейты между направлениями

Гейты дисциплинируют переход. Без них есть риск «уже почти A готово,
дай начнём B» с накапливаемым техническим долгом.

### Внутри A (между шагами A.1 ... A.7)

После каждого шага — минимальный smoke перед началом следующего:

- PR смержен в `macos/blackden/master`, auto-ship отработал, бинарь установлен
- Запуск + 10 минут CoP gameplay без видимых regression
- Save → quit → relaunch → load — round-trip чистый
- `~/Library/Logs/OpenXRay/openxray.log` без новых ERROR/WARN на старте

Эти гейты лёгкие — не блокеры между шагами, а «не делай следующий PR
пока не подтвердил что предыдущий не сломал базовое».

### Гейт A → B (полный переход)

Это серьёзный гейт. Не пройден — доделываем A, не начинаем B.

- [ ] Все 7 шагов A.1 ... A.7 в `macos/blackden/master`
- [ ] `otool -L bin/arm64/ReleaseMasterGold/xr_3da` не содержит libSDL2
- [ ] #99 закрыт реально, не «wont-fix-at-SDL-layer» (Cmd-Tab → Space stickiness, не minimize в Dock)
- [ ] Family 1 hangs verified: PR #76 reproducer (`nc -l 8000` блокирующий sendto) **не вешает систему**. Это структурное доказательство что A.2 (NSRunLoop) сработал
- [ ] 30 min непрерывного CoP gameplay на M1 — zero crash, zero hang
- [ ] Save/load 100× soak (`scripts/soak/save_load.sh`) — 100/100 cycles clean (regression check vs baseline 2026-05-20)
- [ ] Stable promote возможен: `make promote` отрабатывает чисто, `/Applications/OpenXRay.app` (stable) обновляется до нового SHA, играется
- [ ] Нет открытых P0/P1 в `notes/playbooks/issues-playthrough.md`
- [ ] `notes/conventions/done-criteria.md` обновлён под native build (часть пунктов 1.x потребует переформулировки — например, «SDL2 в bundle» перестаёт быть критерием)

### Гейт B → C

Лёгкий гейт — B и C почти независимы, основной смысл — порядок ради
концентрации.

- [ ] `macos-native.md` фазы 1-2 — все галки
- [ ] Фазы 3 и 5 — базовый набор (input layouts, dock click, стандартные shortcuts, VoiceOver не падает)
- [ ] Никаких UX-regression обнаруженных в ежедневном использовании
- [ ] Готов считать продукт «нативно ощущается» на собственном опыте

### Гейт «всё done» (закрытие эпика #87)

- [ ] `macos-native.md` полностью кроме phase 6 (sandbox — explicit non-goal)
- [ ] DMG раздаётся публично, чистый Mac M-series открывает через двойной клик без warnings
- [ ] `spctl -a -v dist/OpenXRay-AllInOne.dmg` → accepted
- [ ] Updater работает вне VPN (если решено через Sparkle в C.5)
- [ ] `done-criteria.md` обновлён под новую реальность (native build, signed/notarized distribution)
- [ ] Эпик #87 закрывается, открыты child-issues только если новые задачи

## 6. Что НЕ входит в этот roadmap

### Технические направления не для этого эпика

- **Metal renderer** — Phase 2 4-year roadmap, отдельный многомесячный эпик после нативного rewrite. Текущий GL backend через Metal translation остаётся
- **Vulkan via MoltenVK** — то же, отдельная фаза
- **App Sandbox** — explicit non-goal в `macos-native.md` phase 6
- **App Store distribution** — требует sandbox, поэтому non-goal
- **Universal binary (arm64 + x86_64)** — нет потребности (M1 mini, M3, нет Intel Mac в обиходе)
- **Native gamepad через GCController** — нет запроса, играешь с клавиатуры. Если появится — отдельный issue
- **Touch Bar** — deprecated by Apple, нет железа
- **iCloud sync / Game Center / GameKit** — out-of-scope форка
- **Rewrite в Rust+ECS** — hypothetical из 4-year roadmap, не actionable
- **CS / CoC parity на native shell** — фокус только на CoP per `done-criteria.md`
- **Windows-side fixes** — fork posture macOS-only

### Что НЕ ломаем (сохраняем как было)

- Vanilla gameplay constants (timing, damage curves, balance) preserved
- Modding workflow через overlay tree (`<install>/gamedata/<path>`) сохраняется
- Lua / luabind / LuaJIT scripts — unchanged
- LTX / INI config format — unchanged
- `.db` archive format + xrUnpack tooling — unchanged
- ALife save format / versioning — unchanged (любые изменения требуют bump `ALIFE_VERSION` per `notes/conventions/save-format-policy.md`)

### Отложенные открытые вопросы

- **Rename продукта** — open question из issue #87 section 3. Решается в C.6 (когда дойдём до signed distribution, тогда natural moment назвать продукт по-новому)
- **Monetization via Boosty/Patreon** — parked в `notes/strategy/roadmap-4year.md` до закрытия эпика #87
- **AppKit run loop как trigger для async refactor #72** — частично перекрывается A.2. Решение «закрываем ли #72 как поглощённый A.2» отложено до завершения A.2

## 7. Эскалация и расширение команды

Если по ходу A.1 ... A.7 окажется что справляемся медленнее
ожидаемого, или встречаем неожиданные блокеры (например, обнаружится
что аудит A.5 не no-op, или что Family 1 hangs остаются после A.2,
или что input в A.3 ломает что-то нетривиальное в xrInput) —
вызывать `team-lead` agent для adversarial review.

Возможные исходы такой эскалации:

- Расширить штат специализированных agents (например, аналог
  `apple-platform`, но узкоспециализированный на AppKit input или
  Core Audio)
- Перестроить порядок шагов внутри A
- Парковать направление и переходить к B/C параллельно
- Признать невыполнимость подхода и вернуться к подходу 2 (теневой
  билд) или 3 (feature-ветка)

Не принимать такое решение в foreground без team-lead consilium —
это cross-cutting decision.

## 8. Связано

- Эпик: [#87](https://git.fedorov.tech/ragnar/xray-16/issues/87)
- Brainstorm origin: [#106](https://git.fedorov.tech/ragnar/xray-16/issues/106) (этот roadmap)
- Hang taxonomy (Family 1 structural justification): [`notes/decisions/hang-taxonomy.md`](../../../notes/decisions/hang-taxonomy.md)
- Native checklist: [`notes/reference/macos-native.md`](../../../notes/reference/macos-native.md)
- Done criteria: [`notes/conventions/done-criteria.md`](../../../notes/conventions/done-criteria.md)
- 4-year roadmap context: [`notes/strategy/roadmap-4year.md`](../../../notes/strategy/roadmap-4year.md)
- apple-platform agent (owner работы): `.claude/agents/apple-platform.md`

## 9. Что дальше

После approval этого roadmap:

1. Этот PR мержится в `macos/blackden/master`
2. Отдельной сессией — `superpowers:writing-plans` skill для шага
   **A.1** (NSWindow + NSApplicationDelegate). Это даёт concrete
   implementation plan: какие файлы трогаем, какие .mm/.hpp создаём,
   как ведём проверку, какие тесты добавляем
3. Имплементация A.1 — через `apple-platform` agent с конкретным планом
4. После merge A.1 — следующая сессия для A.2, и так далее

`team-lead` agent вызывается перед dispatch'ем каждого шага для
validation подхода.
