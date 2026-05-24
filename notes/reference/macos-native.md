# macOS native — что значит «нативное приложение»

Skeleton checklist для перехода от текущего state («engine ships as a process inside an .app bundle, SDL2 owns the window») к **fully native** mac app (own NSApplication delegate, native menu bar, Hardened+notarized, HIG-conforming). Полная работа — несколько сессий после meta-cleanup'а 2026-05-24; этот файл — мерка готовности.

Связано: [`apple-silicon.md`](apple-silicon.md) (ARM64 specifics), [`macos-build-guide.md`](macos-build-guide.md) (как сейчас собирается), [`macos-distribution.md`](macos-distribution.md) (как сейчас ставится), `.claude/agents/apple-platform.md` (agent с полным scope).

## Definition

«Native macOS app» здесь = такое приложение, которое поведением неотличимо от референса в категории single-player PC games на mac (Civilization VI, Disco Elysium, BG3, Death Stranding). Игрок не должен **замечать** что это порт.

## Checklist (по фазам)

### Phase 1 — App lifecycle (foundation)
- [ ] Свой `NSApplicationDelegate` (не SDL'овский), правильно обрабатывает `applicationShouldTerminate:` (Cmd+Q открывает pause-menu, повторно — graceful shutdown)
- [ ] `applicationWillTerminate:` дренирует engine state без хука `atexit` (текущая Cmd+Q logic в `macos_cocoa_shim.mm` — переезжает сюда)
- [ ] Sleep/wake: `NSWorkspaceWillSleepNotification` пауза рендера, `NSWorkspaceDidWakeNotification` восстановление GL/Metal context
- [ ] Background/foreground transitions (`applicationDidBecomeActive:` / `applicationWillResignActive:`) — корректно паузит игру при потере focus, опционально
- [ ] Restorable state — игрок возвращается в окно после Mission Control / Spaces switch без потери

### Phase 2 — Native UI
- [ ] Native menu bar — Application/File/Edit/View/Window/Help с правильными стандартными items
- [ ] Cmd+, открывает Preferences (engine options screen)
- [ ] Native About dialog (`NSStandardAboutPanelOptions`) с build SHA, version, credits
- [ ] Cmd+W / Cmd+H / Cmd+M работают как ожидается (close window / hide / minimize)
- [ ] HiDPI / Retina perfect — backing scale factor handling, никакого мыла
- [ ] Window content resize без визуальных артефактов

### Phase 3 — Input & game controllers
- [ ] `GCController` framework для native gamepad (вместо SDL game controller)
- [ ] Trackpad gestures (pinch zoom если применимо, swipe)
- [ ] Native клавиатурный input — поддерживает все layouts (cyrillic, etc.) без SDL preprocessing
- [ ] Input Monitoring permission — если нужно (для глобального hotkey capture) — один раз prompt с понятным «зачем»

### Phase 4 — Signing & distribution
- [ ] Developer ID signing (не ad-hoc) для shipping bundle
- [ ] Hardened Runtime включён, entitlements минимальный набор (`com.apple.security.cs.disable-library-validation` для unsigned dylibs, `com.apple.security.cs.allow-jit` для LuaJIT)
- [ ] Notarized via `notarytool` + stapled via `stapler staple`
- [ ] Gatekeeper passes — `spctl -a -v` на distribution bundle возвращает «accepted»
- [ ] Sparkle (или эквивалент) — auto-update channel (когда переходим с VPN-only updater на public)
- [ ] DMG codesigned — `codesign --sign ... <out>.dmg` после `hdiutil create`

### Phase 5 — Accessibility & HIG
- [ ] VoiceOver навигация по native menu (минимум — не блокирует, не падает)
- [ ] Dynamic type/accessibility size — game-UI scales (если применимо)
- [ ] Reduced motion preference уважается (если в render есть motion-heavy эффекты)
- [ ] Standard macOS keyboard shortcuts работают (Cmd+M minimize, Cmd+H hide, Cmd+W close)
- [ ] Dock icon click — focus на existing window, не создаёт новое
- [ ] Mission Control / window snapshots выглядят корректно

### Phase 6 — App Sandbox (deferred / optional)
- [ ] Решение: enable App Sandbox or stay out
- [ ] Если enabled — refactor `LocatorAPI` для sandbox-friendly FS access
- [ ] User-selected file access via `NSOpenPanel` для save/load/import дорог
- [ ] App Store-ready если выбрали этот distribution channel

## Что НЕ входит в «native»

- **Metal renderer** — отдельная огромная работа (Phase 2 4-year roadmap), не часть native-app definition. Текущий GL backend через Metal translation — приемлемо.
- **Multiplayer / GameSpy** — out of scope этого форка
- **Game Center / iCloud sync** — out of scope unless explicitly requested
- **Touch Bar support** — у пользователя M1 mini, нет тачбара; deprecated by Apple anyway

## Текущее состояние (snapshot 2026-05-24)

| Phase | Status |
|-------|--------|
| 1 (App lifecycle) | partial — Cmd+Q работает через `macos_cocoa_shim.mm` (SDL wrap pattern), sleep/wake не handled, restorable state нет |
| 2 (Native UI) | absent — SDL menu only; нет About / Preferences pattern; HiDPI fixed (drawable clipping) |
| 3 (Input) | absent — SDL для всего; native game controller нет |
| 4 (Signing & distribution) | partial — ad-hoc codesign работает, Hardened Runtime + notarization нет, DMG не подписан |
| 5 (Accessibility & HIG) | absent |
| 6 (App Sandbox) | explicit-out — не нужно сейчас |

## Gate для старта native rewrite (exit criteria из Issue D)

До того как мы начнём ломать SDL, должна быть **проверенная** playable baseline:

- [ ] Save/load работает 100×, zero data loss
- [ ] No crashes за 30 минут CoP gameplay на M1
- [ ] FPS не хуже X (TBD после measurement) на M1 с medium settings
- [ ] Stable release track active (есть откатовая версия в `/Applications/OpenXRay.app`)

Эти критерии — gate. Не пройдены → доделываем baseline сначала. Полные goals определяются в gitea issue (Issue D плана meta-cleanup 2026-05-24).
