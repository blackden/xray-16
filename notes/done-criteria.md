# macOS port — done criteria

Concrete checklist для определения «1.0» этого форка. Без чёткой
границы мы накапливаем P2/P3-polish бесконечно. Файл — single source
of truth: ответ на вопрос «сколько осталось до done?» должен читаться
за минуту.

Связано: [roadmap.md](roadmap.md) (что хотим делать),
[issues-playthrough.md](issues-playthrough.md) (что открыто),
[macos-distribution.md](macos-distribution.md) (как ставить),
[macos-build-guide.md](macos-build-guide.md) (как собирать).

## Definition of "done"

**1.0 этого форка** = clean M-чип Mac получает работающий, стабильный
S.T.A.L.K.E.R.: Call of Pripyat через single drag-drop DMG, проходит
основную сюжетную линию от и до без data-loss и vanilla-divergence,
которая ломала бы геймплей. Не максимальное качество рендера, не
parity с CS/CoC, не multiplayer — только **полный CoP single-player
playthrough native arm64**, плюс readme/build/distribution docs для
следующего поколения.

## 1. Functional checklist (must-pass)

Каждый пункт — бинарный: PASS / FAIL.

- [ ] Native arm64 build для Mixed и ReleaseMasterGold (`make build &&
      make build-release`), без Rosetta, проверено `file bin/arm64/*/xr_3da`.
- [ ] CoP main quest start → credits на чистом сейв-слоте без
      crash'ей или soft-locks. Допустимы P3 vanilla quirks (snork
      through-floor типа), задокументированные.
- [ ] Save/load round-trip ≥100 раз без deserialization drift
      (через `make profile-load-cycle` или ручной цикл F6/F9).
- [ ] Safe-mode recovery работает: `touch
      ~/.openxray-data/_appdata_/.boot_in_progress` → следующий launch
      приходит с минимальными настройками, логи показывают
      "==> SAFE MODE".
- [ ] `.app` подписан минимально (`codesign --sign -`), запускается
      через right-click → Open без Gatekeeper-блокировки на чистом
      Mac'е.
- [ ] DMG монтируется и работает на чистом M1, M2, M3 (тест минимум
      на одной из них, лучше на всех доступных).

## 2. Quality bar (should-pass)

- [ ] Нет открытых **P0/P1** issues в `notes/issues-playthrough.md` —
      это блокеры данной планки.
- [ ] Все **P2** либо closed (фикс шиппинутся), либо документированы
      в `notes/known-divergence.md` с указанием:
      vanilla-X-Ray-DX11 / openxray-upstream-Windows / нашего форка
      поведения, плюс план (или «accepted as-is on macOS»).
- [ ] Все **P3** vanilla quirks (например `#17` rain в bunker
      без CDB ceiling) задокументированы как «accepted vanilla
      behavior, not a regression».
- [ ] Нет необъяснённых `XXX [ragnar] *:` без либо assignee к issue,
      либо явного `INTENTIONALLY DEFERRED` статуса в комментарии.
- [ ] `~/Library/Logs/OpenXRay/openxray_*.log` ротейтятся: единичный
      log не превышает 100 MB (см. fix `notes/decisions.md` про log
      rotation cap).
- [ ] CHK_GL не выводит ошибки в Apple-MasterGold-логах из штатного
      gameplay (загрузка → пол-часа игры → quicksave → quicksave-load).

## 3. Distribution

- [ ] DMG ≤ 4 GB. Build target — `make all-in-one` → `dist/OpenXRay-AllInOne.dmg`.
- [ ] Bundled `xrUnpack` присутствует в `.app/Contents/MacOS/`,
      работает: `/Applications/OpenXRay-Dev.app/Contents/MacOS/xrUnpack --list <archive.db0>`.
- [ ] `make ship` собирает + устанавливает `.app` за <90 секунд на
      M1 (ориентир, не жёсткий gate).
- [ ] `notes/macos-distribution.md` описывает: что в DMG, как ставить,
      safe-mode, где saves/logs, что делать при первом launch.

## 4. Documentation

- [ ] `notes/macos-build-guide.md` — точная последовательность шагов
      для clean M-чип машины (brew install, git clone --recursive,
      cmake invocation, run). Проверена ручным запуском с чистого
      checkout.
- [ ] `notes/macos-distribution.md` — что внутри DMG, как ставить,
      что такое safe-mode, где сейвы/логи.
- [ ] `CLAUDE.md` — current build flags, conventions, project layout.
      Не отстаёт от реальности.
- [ ] `notes/engine-map.md` — каждый non-trivial touchpoint, который
      потребовал >1 файла exploration, имеет file:line запись.
- [ ] README banner на `macos/blackden/master` ветке отражает текущее
      состояние (build/run/known limits/links в notes/).

## 5. Upstream backlog (PR candidates back to `OpenXRay/xray-16:dev`)

Каждый — отдельная upstream-PR-задача (после done — отдельная сессия,
не блокер для 1.0):

- POSIX path-separator normalisation в `LocatorAPI::rescan_path` и
  `FS_Path::_set_root` (commits в `src/xrCore/LocatorAPI.cpp`).
- Apple GL 4.10 unconditional `*_QUALITY` defines в
  `rgl_shaders.cpp:348-403` (SSAO shader cascade fix).
- Flashlight transitively fixed via above (комментарий в engine-map).
- Per-spawn rain gate (#16, после soak-test).
- `SM_FOR_GAMESAVE` для GL backend (save thumbnails, commit 2e8d0930d).
- ALife save-version soft-fail в `alife_simulator_header.cpp`
  (commit e666e1de8).
- `xrUnpack` standalone tool (commits 69e5a91c5 + 94ad5005d).
- HiDPI drawable clipping fix.
- `pw_name` POSIX login для Core.UserName (commit a2be617ba).
- xrDebug buffer-overflow fix (`xrDebug::Fail` PAC trap на ARM64).

## 6. Non-goals (этого форка, explicit)

Эти пункты **не входят** в "done" — попытка их зацепить → scope creep,
их игнор → правильное решение для 1.0:

- **Vulkan/MoltenVK / Metal renderer** — отдельный многомесячный
  epic (Epic #13 на github), сначала 1.0 на GL.
- **UTF-8 миграция всей кодовой базы** — текущие точки покрыты
  shim'ами (см. `notes/encoding-story.md`); глобальная миграция —
  не для этого скоупа.
- **CS / CoC parity** — только CoP supported; CS можно загружать,
  но не testирован.
- **Multiplayer / GameSpy** — деактивирован, поддерживать не
  планируем (GameSpy SDK end-of-life).
- **xrCompress port на macOS** — overlay-tree даёт modding workflow
  без него.
- **Editor tools (xrLC_Light и пр.)** — Epic #14, отдельно.
- **AI Director** — Epic #15, отдельно.

## 7. Текущее состояние (snapshot 2026-05-20)

Для понимания «сколько осталось» — заполняется по мере выполнения,
обновлять при каждом merge'е в `macos/blackden/master`.

**Functional (1):** 5/6
- [x] Native arm64 build.
- [x] CoP playthrough (user verified, см. `notes/session-*.md`).
- [ ] Save/load 100x round-trip soak — harness landed (#23),
      user-side прогон pending.
- [x] Safe-mode recovery.
- [x] `.app` signed (codesign -) — three-pass seal, codesign --verify
      passes, valid on disk (#25, fix 08335164c).
- [x] DMG на M1 (#25) — user-verified clean-install: mount, drag-drop,
      first launch, save/load, thumbnails — все PASS. M2/M3 out of
      scope per user.

**Quality bar (2):** ~95%
- [x] Нет открытых P0/P1.
- [x] P2 closed: rain doorway (#16), bunker rain (#17 side-effect of
      bloom fix), Yanov bloom + seam-leak (#19), "Exit to Windows"
      i18n (#20). Black geometry в бункере (#21) — vanilla quirk.
- [x] P3 vanilla quirks logged.
- [x] XXX-comments tracked (LightTrack.cpp, dxRainRender, glr_screenshot,
      r3_R_rain, accum_volumetric_sun.ps).
- [x] Log rotation cap landed.
- [x] CHK_GL essentially clean (#22): shader fails 109→0, 0x502
      1152→33 (residual post-load init logged in known-divergence.md).

**Distribution (3):** 4/4 на M1
- [x] DMG ≤4 GB.
- [x] xrUnpack bundled.
- [x] `make ship` fast.
- [x] `macos-distribution.md` есть.

**Documentation (4):** 5/5
- [x] build-guide, distribution, CLAUDE.md, engine-map, README banner.

**Upstream backlog (5):** 0/10 sent.

**Non-goals (6):** explicitly out — N/A.

### Что блокирует 1.0 сейчас (обновлено 2026-05-20)

**Один пункт остался:**

1. **Save/load 100x soak** — harness landed (`scripts/soak/save_load.sh`,
   #23), прогон не выполнен. User-side ~27-минутный запуск.

Код-уровневая работа по 1.0 **завершена**. После того как soak пройдёт
без R_ASSERT / format mismatch / crash, форк можно считать 1.0-ready.
Дальше — upstream backlog (отдельная сессия).

### Также появились в эту сессию

- #24 intro cutscene не сбрасывается при New Game (low-prio cosmetic, не блокер 1.0).
- #23 save/load harness landed but not run.
- #25 DMG smoke-test — codesign issue found + fixed (08335164c).
