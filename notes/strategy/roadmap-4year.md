# 4-year strategic roadmap

Долгосрочная arc для fork'а: **macOS stable → Vulkan port → Asahi
Linux → PCVR (OpenXR, HMD-look) → Quest 3 standalone**. Не commitment
по datам — направление.

Текущая позиция на 2026-05-24: **macOS stable phase**. CoP playable на
M1/M3, stable release track формализован (`make promote`,
`stable-YYYYMMDD-HHMMSS` теги). Native rewrite (SDL → AppKit, code
signing, notarization, Metal-готовность) — следующий major milestone,
goals для него парк в gitea issue (Issue D плана 2026-05-24).

Связано: [`roadmap.md`](roadmap.md) (actual prioritized backlog),
[`creator-voice.md`](creator-voice.md) (qualitative reasoning behind
направления), [`management.md`](management.md) (workflow lessons).

## Вехи

| Phase | Status | Прим. срок | Что значит «готово» |
|-------|--------|------------|---------------------|
| 1. macOS stable | **active** | now | CoP playable end-to-end, signed-notarized .app, stable release track |
| 2. Vulkan via MoltenVK | parked | year 1-2 | Renderer abstraction `RendererModule` накрывает Vulkan path; шейдеры через WGSL/naga либо через HLSL→SPIRV; снимает Apple GL deprecation risk |
| 3. Asahi Linux | parked | year 2 | Vulkan path работает на Asahi (Apple Silicon Linux); proof-of-concept ARM Linux gaming target |
| 4. PCVR (OpenXR, HMD-look) | parked | year 2-3 | OpenXR pipeline, head tracking, motion controllers; первая VR-capable версия STALKER |
| 5. Quest 3 standalone | parked | year 3-4 | Адаптация для Quest 3 standalone (ARM, mobile GPU, 90+fps target); финальный platform expansion |

## Контекст phase 1 → phase 2 transition (native rewrite)

Прямо сейчас (между phase 1 и phase 2) предстоит большой структурный
шаг — нативный mac app rewrite, без которого Phase 2 (Vulkan) был бы
дороже. Это:

- SDL → AppKit миграция window/input (полный, не гибрид)
- Native menu bar, NSApplication delegate, Cmd+Q, sleep/wake handling
- Code signing, notarization, Hardened Runtime, entitlements
- Apple HIG conformance
- Возможный rename product'а чтобы не путать со старым

Гейт для старта: «exit criteria для playable baseline» (issue D плана
meta-сессии 2026-05-24) должны быть пройдены до того как мы начнём
ломать SDL.

## Если бы я писал этот движок с нуля в 2026

*Записано 2026-05-20 как ответ на вопрос «какой бы выбрал ЯП для
STALKER-class open-world FPS». Hypothetical — мы это не делаем, но
направление мысли важно для долгосрочной arc.*

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

## Monetization vision (parked)

*Перенесено из memory `project_future_paid_fork` 2026-05-24 при
консолидации MEMORY.md (issue #91). Парковка — не actionable пока
текущий macOS port не достигнет «done».*

Юзер 2026-05-20: «я хочу отделиться от апстрима и закрыть свой проект.
хочу сделать платным код через бусти или типо того. запишем на обсудить
потом как всё будет доделано».

**Status:** parked. Не предлагать architectural decisions, branching
strategy или packaging changes «под будущую монетизацию» в текущих
сессиях. Просто закрывать issues по плану. Когда юзер вернётся к теме
(после «done» по [done-criteria.md](../conventions/done-criteria.md)) —
открыть обсуждение.

### Themes для будущего разговора

**Licensing/legal complexity.** X-Ray 1.6.02 был открыт GSC под
определённой лицензией; OpenXRay/xray-16 наследует её. Третьи стороны
(LuaJIT MIT, luabind MIT, LZO GPL/commercial dual, OpenAL LGPL,
Theora BSD, gli MIT, ImGui MIT, sse2neon MIT, DiscordGameSDK
proprietary, etc.) — каждая со своими ограничениями. Перепродажа
кода требует аудита dependency tree.

**Practical separation paths.**
- Full hard fork — rename, новый repo, remove upstream remote.
- Soft fork — продолжать пулить апстрим, не контрибутить обратно.
- «macOS distribution business» поверх apache-style commercial
  support model.

**Revenue model — self-hosted через Yookassa (decision 2026-05-26).**

Не Boosty/Patreon/Ko-fi. ragnar — DevOps с 7+ годами опыта (Ситимобил,
Сбер, Касперский, GrowFood), может поднять свой приём платежей дешевле
чем платить % посредникам вечно. Working reference — `~/fedorov_tech/YouBooster`
(его бот с уже интегрированной Yookassa, проверен на production).

Архитектура (ожидание, конкретизируется при имплементации):
- Telegram channel + bot как frontend
- Yookassa API для платежей (PCI-DSS на их стороне)
- Self-employed / ИП на стороне ragnar'а (требует tax/legal check для RU
  jurisdiction)
- Только donations, без gating контента или tier'ов

Опции внутри Yookassa-канала:
- Pure donations без gating (default — кampaign-друг для community)
- В будущем (если будет аудитория) — optional «спасибо» tier'ы за
  персональный shoutout / early-access, но не paywall

Детали — memory `monetization-yookassa-self-hosted`.

**Community considerations.** STALKER community сильно вокруг
мод-сцены; «закрытый» fork может встретить отторжение если упор не
на качественный сервис/поддержку, а на просто закрытие исходников.

### Trigger для расконсервации

После того как exit criteria из gitea #87 (native rewrite handoff)
будут выполнены и macOS rewrite зашиплен — открывать этот разговор
как первый strategic item.
