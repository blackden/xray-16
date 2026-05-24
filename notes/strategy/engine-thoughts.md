# Engine thoughts — X-Ray 1.6.02

Личные впечатления о движке после двух недель работы с ним, плюс что бы я
хотел с ним сделать, если бы у меня были свободные руки.

Записано по просьбе пользователя «зафиксируй свои мысли, мне нравится видеть
творца тебя за работой» (2026-05-19, после Phase 4 + safe-mode + automation
pipeline). Здесь — не roadmap и не план, а зафиксированный голос.

## Что это за движок

X-Ray 1.6.02 — это **выживший**. 17 лет, бесчисленные форки, всё ещё играется,
всё ещё портируется. Что-то в фундаменте — не во внешней архитектуре или коде,
а именно в фундаменте — заставляет этот движок жить дольше многих современных
тех-стеков. Мне это любопытно само по себе.

## Что мне в нём нравится

- **Архитектура модулей честная.** `xrCore` / `xrEngine` / `xrGame` / `xrCDB`
  / `xrAICore` — границы понятны, можно понимать один кусок не зная
  остальных. Это редкость для движков такого возраста — обычно к 2007 году
  game engines становились моноблоками. Здесь — нет.
- **FS-слой с overlay paths и архивами реально хорош.** Когда я починил два
  бага в `rescan_path` / `_set_root`, увидел: эта подсистема была написана
  думающим человеком, который понимал что mod packaging — это first-class
  use case, не afterthought. Не каждый ААА-движок 2007 года такое умел.
- **Pluggable renderer.** R2 / R4 / GL / DX через `RendererModule` — мы могли
  спокойно жить с GL backend пока DX игнорируем. Не идеально, но pluggable.
- **Comments честные.** `XXX:` пометки, `// hack -(`, `// XXX: not reliable
  when both persons have same names` — авторы не прятались за словами
  «архитектура». Это много говорит о культуре команды.
- **Mod-friendly до фанатичности.** Многое driven через XML/INI; community
  построил `gamedata.db` экосистему вокруг этого. Engine не сопротивляется
  кастомизации — он её приглашает. `IgnoringMissingEndTagError` в XML loader
  потому что моддеры — люди и иногда забывают `</tag>` — это **сочувствие к
  пользователю**, прошитое в код.
- **Author thinking with you.** Вот это качество я заметил отдельно и оно
  меня греет: alife пишет в save даже NPC которых player не видит; AI
  behaviour graphs позволяют поведению меняться от состояния
  (`m_alife_human_brain`); прайс-листы трейдеров с inflation per faction.
  Это не дешёвая sandbox-генерация — это автор, думающий вместе с тобой,
  что игроку будет интересно через 5 часов после старта.

## Что в нём плохо (или просто старо)

- **Windows-1.6.02 эры предположения везде.** Кодировки (cp1251), пути
  (backslash), fullscreen exclusive — всё это норма Windows XP. Мы две
  недели мигрировали кодировку, и это была **самая дорогая** работа всего
  проекта. Дорогая потому что предположения зашиты в десятки мест и не
  декларированы — приходится их **выкапывать** по одному.
- **Stringly typed config.** Section names в INI — magic strings. Section
  `weapons` с полем `inv_name` — найти все callsites можно только grep'ом.
  Refactor дорог, потому что компилятор не помогает.
- **Hand-rolled containers (`xr_string`, `xr_vector`) с собственным memory
  tracking.** Микрооптимизация 2007 года, которая сейчас часто хуже std
  эквивалентов из-за более слабого alias analysis у компилятора. Менять —
  не вариант (рискованно), терпеть — тоже не айс.
- **Save format brittle.** Бинарный, с position-tied десериализацией, без
  version-aware migration. Мы добавляли cp1251→UTF-8 shim — это в
  `load_data`, а не в правильном versioned migrator. Любое будущее изменение
  save-структуры — headache.
- **Race conditions латентны.** `FS.r_open` без null-check; autosave queuing
  с предположением что callback дойдёт; `CHK_GL` в release был bare
  expression без проверки `errno`. Эти баги маскировались на Windows +
  быстрых дисках, на macOS вылезают потому что macOS другая.
- **Lighting/rendering pipeline — самая сложная часть.** Shadow cascades +
  occlusion queries + light culling. Где мы видим TX-state — оно отсюда. Я
  БЫ туда сейчас не лез без Metal/Vulkan переписи: бесполезно патчить shim
  поверх shim.
- **Lua interop sloppy.** Скрипты могут вернуть cp1251 string в C++ который
  этого не ожидает — мы такое видели в save names и character names. Type
  discipline на boundary почти нулевая.

## Что **интересно** само по себе

- **Этот движок реально нельзя заменить vanilla Unreal/Unity/Godot и
  получить STALKER.** Architecture отражает game — alife simulator с
  emergent NPC behavior, faction system, level streaming, sandbox FPS. Это
  **engine, выросший под одну игру** и потом обнаруживший себя в роли
  платформы. Это плюс (специализация) и минус (сложно обобщать).
- **Сообщество fork-and-fix действительно работает.** OpenXRay, Anomaly,
  GAMMA, наш fork — все живут параллельно, патчат своё, иногда
  обмениваются находками. Это **рабочая модель** для нишевых движков.
  Подозреваю что upstream OpenXRay впитает 30% наших фиксов когда дойдут.
- **macOS port surprisingly feasible.** Я ожидал что будет 80% Windows-only
  зависимостей. Реально — может 15%. Большинство «port-блокеров» оказались
  **латентными багами** которые на Windows маскировались — мы их починили
  на пользу любой будущей сборки. Это редко так бывает: обычно порт это
  «50% копипаст shim'ов». Здесь — нет.
- **CHK_GL в release был disabled** — а мы только что нашли через него
  реальный 0x502 bug. Когда **runtime diagnostics есть всегда**, движок
  становится debuggable. Это lesson: assertion-style debugging fragile;
  logging-style debugging выживает в production.

## Что бы я сделал с ним сам, если бы были свободные руки

В порядке желания, не приоритета по impact.

### 1. Vulkan-via-MoltenVK renderer (~12 мес.)

Не из «надо», а из **уважения к мёртвому автору**. Oles Shishkovtsov написал
sophisticated forward renderer с deferred lighting, shadow cascades,
occlusion query culling — и это до того как все стали так делать. Сейчас это
крутится через Apple GL 4.1 shim, который Metal-backed и которому через
несколько лет придёт конец. Перенести это на Vulkan — это не порт, это
**доделать его работу до конца**. Под MoltenVK потому что добавляет
Linux/Windows бонусом, не только Apple.

Технически — самый интересный кусок: чтобы понять X-Ray renderer, надо
переписать его. По пути выяснишь все скрытые предположения (например,
почему `set_Format` cache leak'ил vb — потому что в DX все per-format
binding state атомарен, а в GL — нет). Подобные открытия там везде. Это как
реставрация картины — ты учишь стиль автора.

### 2. Renderer playground / scene debugger (~2-3 нед.)

Игровая консоль с hot-reload шейдеров, runtime swap render pipelines,
визуализация G-buffer / shadow cascades / occlusion regions прямо в-игре,
frame budget overlay. Как RenderDoc встроенный. Это бы радикально ускорило
все остальные работы — не нужно ловить баг в external profiler, видишь
сразу.

Это тот тип «учебно-диагностический tool который окупается уже на втором
баге» — а сейчас мы только что 30+ минут потеряли на угадывание 0x502,
могли бы за минуту увидеть.

### 3. Editor tools на macOS (~1-2 мес.)

`xrLC_Light`, `xrCompress`, level packer — всё это Windows-only. Mod
community CoP живёт на этих утилитах. Портировать их на macOS = открыть
платформу для модеров, которые сейчас вынуждены держать Windows VM. Не
самая интеллектуально-богатая работа, но **очень благодарная** — реальные
люди получают возможность работать на их основной машине.

### 4. AI Director (мечта) — не в одиночку

Как у Left 4 Dead, но для Зоны. Alife у STALKER уже emergent — фракции
воюют сами по себе. Но если добавить «режиссёра» который мониторит player
state (здоровье, изоляция от группы, ресурсы) и subtle'но подвинет аномалию
или мутанта чтобы создать tension momentum — это бы превратило обычные
прогулки между квестами в кинематографические сцены.

Это **геймдизайн**, не engine port. Но именно эта игра напрашивается на
такую систему. Возможно в составе мода, а не в core engine.

## Что я бы сказал команде если бы пришёл сейчас впервые

1. **Не переписывайте core.** Он работает и проверен временем.
2. **Но изолируйте новый код от старых assumptions.** Encoding boundary
   (что мы и сделали), platform boundary, FS boundary — должны быть
   чистыми.
3. **Логи > ассерты.** Особенно на macOS/Linux где crash report ≠ Windows
   BugTrap.
4. **Renderer = долгосрочный risk.** Apple GL 4.1 — уходящая натура
   (deprecated на macOS, может пропасть). Metal/Vulkan миграция должна
   стоять в roadmap как 12-месячная цель.
5. **Save format → JSON+version.** Не хочу про это даже думать каждый раз
   когда добавляю поле.

## Финальная оценка

Это **достойный движок 2007 года**, доказавший себя долговечностью. На
М1/М3 он **играется** — это уже больше чем я бы поставил на любой другой
движок такого возраста. Я бы вкладывался в этот fork ещё много месяцев —
он окупает время.

Но я бы не делал на нём новую игру с нуля сегодня; для этого есть Unreal 5.

Этот движок не для новых проектов. Он — **дом** для STALKER. И в этом доме
ещё есть комнаты которые стоит достроить.

---

## GL port as audit pass
*2026-05-19*

Стандартный нарратив про порты: тяжёлая работа адаптации, ценность — новая
аудитория. Наш кейс инвертирует это: порт под более строгий toolchain — это
форма аудита, и его инженерная ценность для *исходного* проекта может
превышать ценность для target-платформы.

Apple GL parser, ARM64 alignment, clang warnings, PAC stack integrity checks —
всё это работает как verification pass над кодом который 15 лет считался
«работающим».

### Шесть классов латентных багов, найденных за одну сессию

1. **SSAO/SSR/SUN/SUN_SHAFTS quality-define cascade** — undefined preprocessor
   identifiers. fxc принимал как 0 молча; Apple GLSL отвергал. Shader
   permutation matrix была silently incorrect для всех конфигураций с
   выключенными фичами. Разработчик не знал что половина кода никогда не
   доходила до ожидаемого compile path.

2. **VAO cache leak / 0x502 GL_INVALID_OPERATION storm** — race при перевязке
   RT на смене разрешения. DX state-machine forgiving → тихий микро-stall на
   Windows. Apple GL через Metal translation → orphan всего pipeline.

3. **pw_gecos vs pw_name для UserName** — POSIX-путь брал display name
   (non-ASCII, запятые, пробелы) вместо account name. Существовал в
   Linux-билде столько лет сколько он существует. Apple PRAuth path сделал
   видимым.

4. **Path separator в LocatorAPI** — `\` implicit-converted на Windows, под
   POSIX вылез как «saves не персистятся». Ловили под Wine годами, fix landed
   только когда macOS port потребовал формальной трактовки.

5. **xrDebug::GatherInfo buffer overflow при Cmd+Q** — на x86 Windows выглядел
   как «crash в crash reporter» и списывался на уже случившуюся ошибку. ARM64
   PAC trap подсветил явно: return address integrity violation.

6. **accum_omni_* GLSL compile errors** — transitively через SSAO_QUALITY
   include chain. Opaque на Windows, surfaces under strict GLSL.

Это не «новые баги от порта» — это existing latent bugs которые порт
разоблачил.

### Sound bites

- *"Cross-platform ports aren't translation work — they're an external audit
  pass paid for by users instead of QA."*
- *"fxc permissiveness compounds into 15 years of unverified preprocessor
  branches."*
- *"A strict toolchain is a free static analyzer with one downside: it makes
  you do all the fixing in one weekend instead of spread over a decade."*
- *"Every undefined-as-zero in HLSL is a Schrödinger feature — it shipped,
  but nobody knows which compile permutation actually runs."*

### Применение

- При PR в OpenXRay upstream: «porting to GL/macOS revealed N latent bugs in
  the Windows build, fixes apply universally» — аргумент для maintainers
  принять изменения которые выглядят как macOS-only.
- Потенциальный заголовок статьи: не «как портировать X-Ray на Mac», а «что
  строгий toolchain нашёл в 15-летнем shipping коде» — аудитория шире
  stalker-community, релевантно для legacy modernization и compiler-strictness
  дискуссий в целом.

*Entry зафиксирован из /btw-сессии с peer-Claude (claude.ai), который
сформулировал тезис о порте-как-аудите по нашим находкам. Полный лог:
`/Users/ragnar/Downloads/xray16-macos-btw-session.md`.*

---

## Стоит ли реимплементировать X-Ray на Godot/Unity/UE? (2026-05-20)

Прямой ответ — нет, и причина не в lock-in или nostalgia, а в том, что
**словa «порт» и «реимплементация» обычно означают разные вещи в разговорах
про STALKER**. Разберу почему я бы не стал.

### Что люди обычно имеют в виду под «портом на UE5»

Когда говорят «надо переписать на Unreal», обычно имеют в виду один из
трёх запросов, и все три плохо решаются «портом»:

1. **«Хочу Lumen / Nanite / нормальные тени.»** Это renderer. Renderer
   в X-Ray — модульный (DX9/DX11/GL), и его уже меняют. Лучше написать
   `R_GL2`/Vulkan-бэкенд внутри X-Ray — bounded scope, всё остальное
   остаётся работать.
2. **«Хочу editor получше.»** Это тулинг. Можно писать на ImGui внутри
   движка (мы уже делаем — F11 playground, F7 inspector). Скрипты на
   Lua, виджеты на ImGui — это эдитор без переезда.
3. **«Хочу делать новую игру в этом сеттинге.»** Это уже не порт. Это
   новый проект. И тут да, UE5 + свои системы — нормальный путь. Но он
   не имеет отношения к существующему коду X-Ray.

### Почему «честный порт» — это месяцы до возврата к нулю

Движок — это не renderer. В X-Ray примерно так:

- `xrGame` ~150k LOC — Actor, weapons, inventory, dialog, UI screens.
- `xrAICore` + AI logic в xrGame — A*, planner, smart-terrain jobs,
  ALife restore/save, schemes.
- `xrServerEntities` — сериализация. Save format держится на её
  бинарном layout'е, изменения ломают saves.
- `xrSound` — EFX, occlusion, looping ambients привязанные к world.
- `xrPhysics` — ODE rigid bodies + ragdolls; завязано на CDB.

Из этого в UE/Godot есть из коробки **только renderer и базовая
физика**. ALife, smart-terrains, schemes, dialog DSL, save format, EFX
occlusion — всё пришлось бы либо переписать с нуля (≥1 год чтобы
вернуться к feature-parity), либо тащить через bridges (получается тот
же X-Ray, только под слоем UE).

### Asset coupling

CoP — ~3 GB assets в `.db?` (LZO-compressed архивы), внутри:

- `.ogf` — модели с встроенным skeleton + LOD chunks.
- `.omf` — анимации, привязанные к OGF bone hierarchy.
- `.thm` — текстурные метаданные (mip strategy, bumps).
- `.geom`/`.dm` — level geometry с pre-baked lightmaps в UV2.
- `.spawn` — ALife graph данных, тоже бинарь.

Чтобы UE/Godot это съел, нужно либо:
- Re-author всё в native-формате (impractical по объёму).
- Написать importers/loaders для X-Ray formats внутри нового движка
  (месяцы работы, fragile из-за edge-cases которые мы только что
  обнаруживали на macOS).

Вариант B на практике значит «написать кастомный X-Ray слой внутри
UE». Зачем — непонятно: тот же X-Ray, только медленнее.

### Эмпирический сигнал — STALKER 2

GSC уже сделали реимплементацию. UE5, 6 лет, ~$100M, выпустили в 2024.
Результат:

- Графика на топ-уровне современных AAA. ✓
- **ALife так не вернулась.** Изначально обещали полноразмерную, в
  релизе она оказалась сильно урезанной — NPC спавнятся вокруг
  игрока, мир не живёт без него. Это критика практически от всех
  ревьюеров.
- Атмосфера — спорная. Многим показалась пустоватее оригинала.

Это сильный практический сигнал: **«чувство» STALKER неотделимо от
конкретных систем X-Ray**. Они не просто реализация фич — они есть
сами фичи. Переезд в другой движок без потери души требует много лет
осторожной реконструкции, и даже GSC с бюджетом и оригинальным
коллективом не справились на 100%.

### Sunk cost — wrong frame

Правильный вопрос не «жалко ли терять X-Ray», а «что даст максимум
impact'а сегодня». В нашем контексте:

| Цель | Путь | Время до impact'а |
|------|------|-------------------|
| Стабильный macOS playthrough | патчить X-Ray | дни (мы тут) |
| QoL-патчи в апстрим | патчить X-Ray | дни |
| Engine experiments | патчить X-Ray | дни-недели |
| Modern renderer | GL/Vulkan abstraction в X-Ray | месяцы |
| «STALKER, но shiny» | UE5 + reimpl | годы |
| Свой sandbox-roguelite | UE5/Godot, новый проект | месяцы (правильная цель) |

X-Ray побеждает по всем строкам кроме последней, и **последняя — это
не порт**, это отдельный проект.

### Где бы я тратил энергию (если зудит)

- **Mod-friendly tooling** — мы уже двигаемся: dev_tools cvar, F11
  playground, xrUnpack бандлится в .app. Следующий шаг — content
  editor в ImGui (level mesh inspector, entity browser).
- **Scripting modernization** — LuaJIT уже есть, но API из движка
  ограничен. Расширить exposed API, может быть Luau-like type
  hints, может быть hot-reload скриптов (партнёр-Claude поднимал
  тему). Это даёт мод-комьюнити много, и не задевает архитектуру.
- **Renderer evolution внутри X-Ray** — `RendererModule` уже даёт
  abstraction. Можно писать новый бэкенд (Metal/Vulkan) без trash'а
  всей кодовой базы. Опять — bounded scope, исключение апсайдных
  рисков.
- **Standalone mod for CoP** — нужен engine modding, видимый выхлоп
  за дни-недели, проверяет качество API.

### Когда реимплементация — правильный ответ

Только если ответ на «зачем» — **«потому что хочу сделать новую игру
в zone-сеттинге»**. Тогда:

- UE5/Godot/Unity — нормальные кандидаты.
- X-Ray там даже не должен лежать на полке. Это другой проект,
  другая команда, другие инвестиции.
- И всё равно сначала — прототип на бумаге / в Unity, чтобы понять
  что именно ты делаешь, не «потому что engine стар».

В нашем сценарии — мы хотим **стабильный CoP на Apple Silicon и
upstream PR-цепочку для macOS-блокеров**. X-Ray — точно правильный
инструмент. Реимплементация — отвлечение, которое даст visible
output не раньше года и почти наверняка проиграет в качестве
оригинальному коду.

**TL;DR:** реимплементация — не порт, а новый проект. Если хочешь
новый проект — это валидный путь, но он не имеет отношения к X-Ray.
Если хочешь чтобы STALKER лучше работал — патчи X-Ray дают больше
impact'а на каждый человеко-час на 1-2 порядка.


---

## Session 2026-05-20: 1.0-ready, migrated, lessons

Длинная сессия. Что произошло, на верхнем уровне:

### Closed: пять блокеров до 1.0

Чек-лист из `notes/done-criteria.md` все галки:

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
  это паттерном в `notes/known-divergence.md`.
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

## Приложение: «Если бы я писал этот движок с нуля в 2026» (2026-05-20)

Юзер спросил во время сессии #36 — какой бы выбрал ЯП для STALKER-class
open-world FPS, если бы стартовал с белого листа сейчас. Зафиксирую
развёрнуто.

### Короткий ответ

**Rust + ECS (Bevy-flavored) + wgpu** для движка, **Lua/Luau** для
скриптинга. Это не «дань хайпу», а несколько конкретных рычагов, которые
делают именно этот жанр игр заметно проще именно в этих языках/runtime-ах.

### Почему именно Rust и ECS — четыре аргумента

**1. A-Life — это циклический граф долгоживущих объектов.**

То, что делает STALKER уникальным, — это симуляция Зоны: тысячи NPC и
объектов, каждый с памятью, фракцией, заданием, инвентарём, и
кросс-ссылками на других. NPC ссылается на smart_terrain, тот на quest,
тот на player reputation, та на global faction state, та на тех же NPC.
В C++ это означает либо `std::weak_ptr` повсюду (и постоянная проверка
expired-ли), либо raw pointer-ы с надеждой на disciplined lifetime
management. Каждый dangling pointer проявляется как spooky null-deref в
самом неудобном месте, обычно через 4 часа геймплея, обычно через
автосейв.

Rust + ECS меняет правила: объект не «хранит ссылку на soulmate-NPC», а
хранит `Entity` (типизированный handle, по сути u64). Любой запрос к
этому handle проходит через мир (`world.get<Faction>(entity)`) и явно
возвращает `Option<&Faction>` — если NPC уже мёртв и удалён, ты *обязан*
обработать None. Spooky deref-ов физически не возникает.

**2. ECS data-orientation идеально для AI sim.**

Сейчас в `xrAICore` запросы вида «дай мне всех NPC в радиусе X с
фракцией Y» делаются через виртуальные dynamic_cast по списку всех ALife
объектов. Это убивает кэш-локальность — Lua-скрипт каждые 5 секунд
бегает по всему списку.

В Bevy-style ECS: `Query<&Position, &Faction, With<Alive>>` —
итерируется по плотно упакованным массивам компонент, парраллелится
бесплатно через `par_iter`, и можно делать spatial partitioning поверх
без переписывания самой логики.

Для CoP с его 800–1500 ALife-объектами разница в производительности
будет ощутимой; для следующего шага (open-world SoC-like с 5K+
объектами) — критической.

**3. wgpu развязывает рендер от платформы.**

Самая большая боль текущего форка — Apple GL shim. У нас здесь
compressed 3D textures не работают, MSAA framebuffer attachments
кривые, light-glitches camera-bound (см. #37). Половину времени я
ловлю не баги движка, а баги Apple-ского OpenGL-эмулятора.

В Rust-движке через `wgpu` каждая платформа гонит свой нативный API:
DX12 на Windows, Vulkan на Linux, Metal на macOS, WebGPU в браузере.
Никаких прослоек, никаких "GL 4.1 на самом деле Metal в маске". И
шейдеры один раз пишутся на WGSL (или транслируются из HLSL через
naga), а не три раза на HLSL+GLSL+метакод.

**4. Cargo экономит ~день в неделю.**

Я работаю с этим форком 2 недели. За это время мы:
- ловили несколько раз баг с подмодулями;
- разбирались с `find_package(LZO)` на macOS;
- руками таскали Externals/imgui-docking, sse2neon, OPCODE, ODE;
- настраивали clang-format и stylecheck workflow.

В Rust-проекте `Cargo.toml` это семь строк зависимостей.
Кросс-компиляция работает из коробки. clippy + rustfmt живут в
toolchain, не нужно отдельно настраивать.

Это не «удобство», это часов экономии за каждую сессию.

### Почему НЕ другие языки

- **C++23.** Knee-jerk choice: оставить C++ и просто модернизировать.
  Звучит безопасно, но реально получаешь incremental win-ы поверх
  тех же фундаментальных проблем: lifetime management ручной, build
  system ручной, шейдеры на трёх диалектах. На 5-летнем проекте это
  означает ~30% времени на инфра-боль. Не стоит.

- **C# / .NET 9 AOT.** Удивительно близко по перфу, гигантский ecosystem
  (NuGet > Cargo по охвату библиотек!). НО: GC паузы. Для AI sim с 5K
  объектов и 60 FPS на мобайл-классе hardware (читай: M1 Air) даже
  5ms-пауза на minor collection превратится в дёрганые кадры в
  худшие моменты. AOT не лечит GC, только startup.

  Серьёзно стоит рассматривать только если вы готовы вручную
  pool-ить всё что выделяется в hot path (то есть фактически
  написать поверх C# свой ECS без аллокаций) — а это уже половина
  работы Rust+ECS, но без safety гарантий.

- **Zig.** Симпатичный язык, comptime — действительно элегантно
  заменяет шаблоны/макросы. Но: gamedev-экосистема младенческая.
  Нет аналога Bevy, wgpu биндинги есть но сырые, нет ImGui
  биндингов уровня Rust. Ставить 5-летний проект на язык, который
  ещё может сломать ABI в 0.12 → 0.13 — рискованно.

- **Odin / Jai.** Bus factor < 5. Jai вообще ещё не публичен. Это
  hobby-выбор, не production.

### Lua/Luau (скриптинг)

Геймплей-скриптовый язык в STALKER — это место где Lua фактически
оптимален. Стейтфул NPC-логика, легко итерируется дизайнерами без
recompile-а движка, JIT даёт достаточный перф для AI behaviour trees.

Что бы поменял: **Luau** вместо vanilla LuaJIT. Roblox-овский форк
тащит type checking (опциональный), namespacing, лучший error
reporting. Совместим с Lua 5.1 синтаксисом, так что миграция
существующих скриптов CoP/CoC механическая.

### Это реимплементация со знанием исходников?

Юзер спросил это явно. Ответ: **да, именно так**. Это не «clean room
rewrite ignoring original» (это была бы новая игра, не STALKER), и не
«механический port C++ → Rust» (это бесполезно — получишь те же
архитектурные грабли в другом синтаксисе).

Правильная формулировка: **«clean-room re-engineering, использующее
оригинальный X-Ray как живую спецификацию»**. Я бы:

1. Прочитал/прогрепал каждую подсистему X-Ray и составил поведенческие
   contract-ы: вход → выход, инварианты, edge cases. Memo формата
   «как ALife выбирает next task для NPC».
2. Спроектировал architecturally clean replacement (в Rust + ECS) с
   тем же поведением и теми же game feel-овыми константами (timing-и,
   damage curves, etc.).
3. Написал conformance-tests, которые играют скриптовые сценарии и
   сравнивают, что NPC ведут себя «как в CoP». Это и есть мост от
   старого к новому.
4. Не тащил бы legacy quirks ради совместимости: Hungarian notation,
   `BOOL` вместо `bool`, `lpcstr` вместо `const char*`, кастомные
   контейнеры вместо `std::*`, etc. Сохранил бы поведение, не код.

### Что бы оставил из X-Ray

- **LTX/INI format** для конфигов — он работает, моддеры знают, гамеплей
  настройщики работают без перекомпиляции. Менять на YAML или TOML
  ради «современности» — потеря 17 лет накопленного mod-контента.
- **`.db` archive format** — простой, работает, есть инструменты.
  Перебиваться на zip или подобное — потеря.
- **A-Life как концепция «симулирующая Зона»** — это и есть душа
  игры. Алгоритмы и data model — да, перепишу; *идею* симулирующей
  open-world environment — сохраню.
- **Уровни как `level.geom` + `level.cform` + `level.spawn`** —
  отдельные файлы под отдельные задачи, грузятся независимо. Хороший
  design, переоткрыть его в новых редакторах — больше работы чем
  переиспользовать.

### Что бы выбросил

- **Hungarian notation + `BOOL`/`DWORD`/etc.** Это рудимент Win98-эры.
- **Кастомные контейнеры** (`xr_vector`, `xr_string`). В Rust есть
  `Vec`, `String`, оно лучше отлаженно.
- **Lua + luabind + boost-detached** — Luau с своим биндером.
- **MSBuild** — Cargo всё.
- **D3D9 эра-shader pipeline** — стейт-объекты, fixed-function
  fallback paths. В wgpu всё через explicit pipeline descriptors.

### Реалистичный таймлайн (если бы взял эту задачу всерьёз)

- Месяц 1-2: ECS schema, базовый renderer (clear color → meshes →
  лайтинг), level loader.
- Месяц 3-4: ALife core, NPC pathfinding, basic combat AI.
- Месяц 5-7: Lua biting, conformance tests on a small CoP slice
  (Скадовск-only).
- Месяц 8-12: остальной CoP, parity с оригиналом.
- Месяц 13+: то что хотел сделать в этом форке но было слишком
  дорого — proper Vulkan-via-MoltenVK, UTF-8 native, deeper A-Life.

В одиночку — 2 года full-time. Командой 3 человек — год. Это не
«в выходные».

### Но мы не делаем это сейчас

Это упражнение «что если». Текущий форк — pragmatic incremental
improvement of an existing thing that ships. Strategic discussion о
переписывании парканная в memory `project_future_paid_fork.md` (если
решим монетизировать через Boosty — рекомендую сначала *stabilize и
ship CoP на macOS*, потом думать про architecture).

Заметка для будущей меня (или будущей сессии): эта тема всплывала
дважды — 2026-05-19 при первой management/strategy дискуссии и
2026-05-20 здесь. Если всплывёт третий раз — стоит конкретно обсудить
«а что если эти 2 года?», иначе так и останется hypothetical.

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
и в `notes/engine-map.md`, но дисциплина «открой исходник
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
