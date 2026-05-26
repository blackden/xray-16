# Management lessons — workflow retrospectives

Workflow-уроки и session retrospectives. Накапливаются по мере того
как мы натыкаемся на дисциплинарные паттерны (что окупилось / что
сломалось / что повторяется). Выделено из `engine-thoughts.md` при
reorg 2026-05-24.

Связано: [`creator-voice.md`](creator-voice.md) (мысли о движке),
[`roadmap-4year.md`](roadmap-4year.md) (стратегические направления),
`notes/conventions/working-agreement.md` (formal rules вытекающие
отсюда).

---

## Session 2026-05-20: 1.0-ready, migrated, lessons

Длинная сессия. Что произошло, на верхнем уровне:

### Closed: пять блокеров до 1.0

Чек-лист из `notes/conventions/done-criteria.md` все галки:

- Rain v3 doorway regression fix (#16) — per-spawn gate + sound floor.
- "macOS done" criteria document (#18) — теперь shipping definition.
- Lighting divergence (#19) — Apple GL bloom default 0.05 (config
  workaround; shader-level root cause logged in known-divergence.md).
- "Exit to Windows" → "...to macOS" i18n (#20) — eng/ukr/ger/pol/cze.
- GL shader compile cascades + 0x502 storm (#22) — fixed
  `accum_volumetric_sun.ps`, dropped error count by 97%.
- M1 DMG smoke-test (#25) — codesign three-pass fix, clean install
  user-verified.
- Save/load 100× soak (#23) — 100/100 cycles, zero errors.
- Pre-push hook (#26) — encoding/whitespace/build sanity.
- Bunker rain (#17) — closed as side-effect of bloom fix.
- Intro cutscene reset (#24) — `CTheoraSurface::Play()` теперь
  zовёт `Reset()`. One-line fix.

### Migrated: GitHub → Gitea

`blackden/xray-16` на GitHub → `ragnar/xray-16` на `git.fedorov.tech`.
Полная миграция: 15 branches, 2 tags, 31 issue с сохранёнными
номерами (#1-31) и комментариями.

`gh` CLI больше не нужен; workflow перешёл на Gitea MCP server
(`mcp__gitea__*` tools). `tea` CLI как offline fallback для cron'а
(`scripts/issues/sync.sh` уже обновлён).

GitHub fork стал private mirror; локально `github-backup` remote
остаётся для безопасности.

### Workflow упражнение: «культура взрослых компаний»

В середине сессии юзер ввёл правило: **каждая задача = issue + branch
+ push + merge**. Включая docs-only. Закрепился через 8 issue'ев на
GitHub-стороне + 4 на Gitea-стороне.

Что замечено: не для безопасности (мы не релизим тулинг другим
разработчикам), а для **читаемости истории через месяцы**. Когда
открываешь Gitea сейчас — видно "что когда чинилось, чем была
мотивация, что осталось". Это окупает overhead ~3 минут per issue.

Persisted в memory `feedback_issue_driven_workflow.md`.

### Surprises и pause-save exploit

Юзер обнаружил soft-lock баг: пауза + console save + console load →
неотклеиваемое заблокированное состояние без UI паузы. Не открыли
issue этой сессии (выходит за scope), но root cause понятен:
`Device::Pause(TRUE, ...)` ставит флаг, save+load пишут/читают
game-state но не флаг паузы; после load input-receiver уже не
прокинут. Логнул как «известный quirk» для будущего.

### Test-rig видение

Юзер заметил soak harness и предложил: выделить отдельный Mac mini
как постоянный QA-rig. Расписал в memory `project_test_rig_vision.md`
три уровня test-sophistication (key macros → keyboard+mouse → Lua
in-engine framework). Lua-уровень — это где можно делать настоящий
regression test framework, но это будущая большая работа.

### Phase 0 для эпиков

Эпики #13/#14/#15 теперь имеют по одной "starter" issue — concrete
deliverable, ~1-2 сессии каждая:

- #28 — MoltenVK hello-triangle (Phase 0 для #13 Vulkan).
- #29 — xrCompress on macOS (Phase 0 для #14 editor tools).
- #30 — AI activity logger (Phase 0 для #15 AI Director).

### Что я почувствовал

- **Issue-driven workflow** — реально лучше. Чтение `git log` без
  issue номеров теперь выглядит как информационный долг. Любую
  серию правок на neighbouring branch'е намного легче ретроспективно
  понимать.
- **Bloom-fix через A/B** оказался дешевле чем глубокий debug
  шейдеров. Когда корневая причина в config, а не в коде — иногда
  workaround за 15 минут лучше чем 3 часа shader forensics. Записал
  это паттерном в `notes/decisions/known-divergence.md`.
- **Migration day** — почти боялся "что-то пойдёт не так с issue
  numbers". Gitea importer оказался безболезненным. Это сильно
  снижает порог для подобных миграций в будущем.

### Что осталось (post-1.0, не блокирует)

- Phase 0 starters #28/#29/#30 — каждая 1-2 сессии.
- pause-save soft-lock — не открытый issue.
- Upstream PRs back to OpenXRay/xray-16:dev — но теперь форк private,
  upstream contributions требуют решения "что хотим отдавать обратно
  и что оставить себе" (см. memory `project_future_paid_fork.md`).

Сессия чувствовалась плотной — закрылось много, но без хаоса:
issue-driven workflow удерживал каждый шаг trackable.

---

## Ретроспектива: smoke-loop как бутылочное горлышко (2026-05-21)

Записано после issue #39 (in-game updater MVP). Пользователь спросил:
«как можно было бы улучшить твою работу, может отдать на аутсорс?»
Я ответил честно, и он попросил зафиксировать — здесь оно.

### Что реально стоило дорого

Главный затык сессии был **не в reasoning**, а в **пропускной
способности smoke-канала**. Один цикл = fix → `make build` →
`make ship` → пользователь прокликивает .app → скриншот/лог. 5-15
минут каждый. На один FS-path баг ушло 4 круга:

1. CInifile отказался парсить URL — `//` в `http://...` — ест как
   line-comment.
2. Mixed slashes в downloaded path — `\Users\...\updates/file.zip`,
   ghttp fopen на macOS ждёт POSIX `/`.
3. `VerifyPath` вызван ПОСЛЕ `convert_path_separators` — он искал `\`,
   их уже не было, ничего не создал.
4. Даже после swap order — `VerifyPath` mkdir'ит только сегменты
   *перед каждым* `\`. Суффикс `updates/file.zip` имеет `/` перед
   `updates`, значит сам каталог `updates/` так и не создаётся.
   Нужен был суффикс с `\\`.

Это не «я медленно думаю» — это **runtime feedback latency**. Если бы
у нас была инструментовка с самого начала или test rig — поймали бы
всё за один прогон.

### Что бы реально помогло (по убыванию пользы)

**1. Log-instrumentation upfront, а не реактивно.** Привычка которую
надо качать: когда я добавляю новый пайплайн (URL → manifest →
expanded path → VerifyPath → convert → fopen), сразу же `Msg("step
X: value=%s")` на каждом шаге. Не по факту провала, а с первой
сборки. Тогда 4 цикла отладки сжимаются в 1.

**2. Read-before-call для незнакомых FS-примитивов.** Я звонил
`VerifyPath`, не прочитав его. Реализация — 10 строк, читается за 30
сек, и сразу видно что mkdir'ит только префиксы до `\`. Этот
конкретный урок зафиксирован в memory `project_engine_fs_path_quirks`
и в `notes/reference/engine-map.md`, но дисциплина «открой исходник
незнакомого FS-примитива прежде чем вызывать» — general-purpose.

**3. Test rig (см. memory `project_test_rig_vision`).** Это
**настоящий мультипликатор** для отладочных сессий. Если бы я мог
автоматически прогнать «Options → Игра → Проверить обновления →
дождаться диалога → скриншот» без пользователя — крутил бы 20
итераций за время одной чашки кофе. Сейчас юзер обязан быть кликером
на каждом круге; это его выматывает не меньше чем меня. Tier-1 даже
из самых простых средств (cliclick/AppleScript + diff скриншотов)
окупится в первой же такой сессии.

### «Отдать debug на аутсорс» — не сработает

Subagent классно для **исследования** (прочитать 200 файлов и
выдать резюме), но плохо для **debug-циклов**, потому что у него нет
smoke-канала. Бутылочное горлышко — не моё мышление, а
runtime-feedback. Больше поваров на кухне не разогревают духовку.

### Где subagent реально окупится

- **Параллельная разведка** пока я кодю: «прочитай весь
  Updater_Manifest pipeline и найди возможные UAF» — субагент сделает
  в фоне.
- **Документирование постфактум**: «возьми этот diff и оформи в
  engine-map». Я часто этим пренебрегаю под конец сессии когда устаю.
- **Независимый код-ревью перед merge** — второй глаз, не видевший
  моих рассуждений.

### Где я облажался конкретно сегодня

- Одна галлюцинация о пути к бинарю — пользователь поймал. Стоила:
  доверие + один круг.
- Я давал «cmake --build» команды вместо `make build` / `make ship`,
  хотя Makefile уже был. Юзер прямо спросил: «ты предлагаешь мне
  вручную собирать?» — справедливо.
- VerifyPath order я перепутал, потом исправил частично, потом
  пришлось ещё раз ловить segment-before-delimiter. Если бы прочитал
  исходник сразу — один фикс вместо трёх.

### Уроки на будущее (для меня)

- Когда вижу незнакомый FS / IO / engine-primitive — открыть и
  прочитать его прежде чем вызывать. Не «авось».
- Log first, debug later. Инструментовка не «когда упало», а «когда
  пишу новый пайплайн».
- Когда мы наступаем на одну и ту же грабли третий раз за сессию —
  это сигнал что нужно либо tooling (test rig), либо durable doc
  (memory + engine-map), а не «ну ещё раз попробую».

---

## Сессия 2026-05-25/26: brainstorm под #87 + meta-обсуждение workflow

Длинная meta-сессия. Что произошло:

### Закрыто: roadmap native shell + manifesto-страта

Три merged PR'а — все docs-only, не блокируют main line:

- **#107** (closes #106) — roadmap `docs/superpowers/specs/2026-05-25-native-shell-roadmap.md`:
  A/B/C decomposition эпика #87, A3 — полное выбрасывание SDL с macOS,
  инкрементальный подход через 7 issue+PR, гейты между направлениями,
  explicit non-goals. Структурное обоснование через hang-taxonomy
  Family 1.
- **#109** (closes #108) — strategic notes bundle: `mentor-principles.md`
  (новый, manifesto работы между ragnar↔claude), `content-system.md`
  (untracked → tracked + Yookassa monetization + timeline fix),
  `roadmap-4year.md` (Boosty/Patreon parking → Yookassa direction),
  `notes/README.md`.
- **#111** (closes #110) — video #1 manifesto skeleton в OTUS-стиле
  ragnar'а (скопирован формат `~/fedorov_tech/otus/linux-basic/docker/lesson/00-skeleton.md`).

Контент-трек **paused** 2026-05-26 явным решением ragnar'а — main line
возвращается к native rewrite. См. issue #110 comment + memory
`[[content-track-paused]]`.

### Что окристаллизовалось

1. **«Не yes-man'ом» — extension of `feedback_invoke_team_lead`.**
   ragnar явно попросил занимать экспертскую позицию по cross-cutting
   решениям, не загружать выбором там где у меня есть экспертное мнение.
   Memory `[[no-yes-man-expert-stance]]`. Plus — само правило симметрично:
   на cross-cutting/process решениях я (foreground claude) тоже не
   доверяю первому instinct'у, вызываю team-lead для adversarial review
   собственного мнения. Это сработало в этой сессии — team-lead surface'нул
   что я зафиксировал roadmap без challenging самого fixation'а.

2. **Mentor-режим = другой жанр от working-agreement.** working-agreement
   = процессы для агентов (when delegate, anti-patterns, etc.).
   mentor-principles = принципы общения с user'ом (controllable vs
   uncontrollable цели, что значит «команда», push на дисциплину).
   Два разных документа, не сливать.

3. **Сессии не идемпотентны на длинной дистанции** (3-6 месяцев native
   rewrite). Team-lead suggested три механизма которые я частично
   применил в этой сессии:
   - **«Rejected alternatives»** в каждый PR description — применил в PR #107
   - **Entry brief** в issue body перед startом шага — пока не применил
     (применю в первой A.N session)
   - **Roadmap changelog** после каждого merged шага — пока не применил
     (применю после A.1 merge)

   Эти три практики — кандидаты для formal'ной записи в
   `working-agreement.md` после реального применения 1-2 раза, не
   theoretical.

4. **«Один шаг A.N = одна fresh session, не /compact».** /compact
   накапливает drift; fresh sessions нет. Каждый шаг A.N открывается
   через skill (auto) + roadmap-документ + предшествующий PR. Это
   следующий test — A.1 будет первой такой сессией.

5. **Skill `long-epic-step` — преждевременно.** ragnar предложил создать
   skill для discipline эпика. Я (correctly) отказал: нет ни одного
   executed шага, skill написанный сейчас будет theoretical bloat. После
   A.1 — base experience есть, после A.1+A.2 — паттерн. Тогда skill.
   Memory `[[tooling-real-not-shelf]]` подтверждает.

6. **Documentation assistant agent — не нужен.** ragnar предложил
   delegated «бумажную работу». Я отказал: объём слишком мал (~10 правок
   за meta-сессию, в обычной работе меньше), документы контекстно-зависимые
   (брифинг agent'а > экономия), general-purpose agent уже доступен если
   нужно bulk replace. Правильный путь — скрипты для деterministic
   bookkeeping (sync-memory-index.sh, check-stale-refs.sh), не agent.

### Что обнаружили факт-чек'ом

- **Timeline проекта:** не «год работы», а **2 недели sprint mode** —
  первый ragnar-коммит 2026-05-14, 272 коммита за 12 дней (по 10-14ч
  в день). Поправлено в roadmap и content-system. Это **более compelling**
  для будущего видео — sprint mode впечатляет больше чем «парень год пилит».

- **Документ `notes/strategy/management.md` накапливает retrospectives**,
  а не процессы. Процессы — в working-agreement.md. Эта запись — пример.

### Куда идём

Главное — fresh session next: `superpowers:writing-plans` для A.1
(NSWindow + NSApplicationDelegate). НЕ продолжать в этой сессии
(контекст загружен meta-обсуждением, разный фокус). Entry brief — в
gitea issue который будет создан для A.1.

### Что я почувствовал

- **OTUS-стиль скелета впечатляющий.** ragnar'ов skeleton-шаблон
  («предполагаем известным», «3 неудобных вопроса», «слабые места»,
  «back-test») — это zrелая педагогическая дисциплина. Перенесённая на
  видео-skeleton работает «из коробки» — поймал 4 gap'а (канал не
  создан, Telegram пустой, Yookassa нет, существующий мак-порт 2025).
  Это паттерн на будущее: при любом content-проекте сначала skeleton в
  его формате, потом scenario, потом demos. Не «придумывать формат
  каждый раз».

- **«Сделай PR'ы пока я читаю» — рабочий pattern для длинных meta-сессий.**
  Foreground делает workflow ceremony автоматически (3 PR за минуты),
  user в это время читает / думает / возвращается с decision. Параллельность
  без блокировки. Это валидирует «foreground = coordinator»
  (memory `[[delegate-to-specialists-via-teamlead]]`).
