# Creator voice — X-Ray engine thoughts

Личные впечатления о движке. Выделено из `engine-thoughts.md` при reorg
2026-05-24. Здесь — не roadmap и не план, а зафиксированный голос «творца
за работой» (по правилу memory `feedback_creator_voice`).

Связано: [`roadmap-4year.md`](roadmap-4year.md) (стратегические аспирации),
[`management.md`](management.md) (workflow ретроспективы),
[`roadmap.md`](roadmap.md) (актуальный prioritized backlog).

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
