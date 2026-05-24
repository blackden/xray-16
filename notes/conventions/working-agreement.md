# Working agreement — ragnar × Claude × subagent team

Зачем этот документ: за пару недель плотной работы мы накопили рабочие
паттерны, но они жили в моей (Claude) голове и в эпизодических feedback'ах
от ragnar'а. Это привело к тому что когда контекст разрастается, я
перестаю надёжно их применять. Этот файл — явный rulebook, к которому
все агенты обращаются. Обновляется в конце каждой сессии если что-то
новое выкристаллизовалось.

Связано: [sub-agents.md](sub-agents.md) (детальный dispatch playbook —
briefing template, parallel agents, anti-patterns),
[done-criteria.md](done-criteria.md) (definition of «done» для 1.0),
[release-track.md](release-track.md) (DEV vs STABLE channels),
[`engine-map.md`](../reference/engine-map.md) (где живёт что в коде),
[`gotchas.md`](../playbooks/gotchas.md) (грабли),
[`creator-voice.md`](../strategy/creator-voice.md) /
[`roadmap-4year.md`](../strategy/roadmap-4year.md) /
[`management.md`](../strategy/management.md) (стратегические записи),
`MEMORY.md` (durable user preferences), `.claude/agents/*` (роли).

## Роли

| Кто | Что делает |
|-----|------------|
| **ragnar** | Owner, primary tester. Strategic decisions. Smoke validation на реальном железе (M1, macOS Tahoe). Финальный merge approval. |
| **Foreground Claude** | Orchestrator + implementer для тривиального кода (≤10 LOC). Управляет dispatch'ом subagents. Финальное communication c ragnar. |
| **`team-lead` agent** | Meta-координатор. Vague-to-actionable translation, convene consilium, strategic review, tool scouting. НЕ пишет код. |
| **`cpp-engineer`** | C++ engine slice (xrCore/xrEngine/xrGame/xrCDB/xrPhysics/etc., НЕ render, НЕ macOS platform). Adversarial review (default) + implementation. |
| **`render-engineer`** | Render layer (`src/Layers/xrRender*`). |
| **`script-engineer`** | Lua/scripting engine (xrScriptEngine, luabind, LuaJIT). |
| **`platform-build`** | macOS packaging, .mm/.m, CMake Apple sections, scripts/mac/, CI. |
| **`Explore` agent** | Read-only codebase research, structure-discovery. |
| **General-purpose** | Multi-step tasks без специализации, parallel research. |

## Когда что использовать

### Foreground Claude делает сам
- Диалог с ragnar (всегда)
- Edit ≤10 LOC, single-file, известный путь решения
- Read/Grep/Glob для быстрой verification subagent output
- Run shell команд (build, ship, git operations) — это его tool
- Merge PR'ов после ragnar'ова approval

### Delegate → specialist subagent
- Code change >10 LOC diff или multi-file — spawn в worktree-isolation
- C++ lifecycle / destructor / RAII / signal-handler audit — `cpp-engineer` (memory `feedback_delegate_cpp_analysis`)
- Render layer (`src/Layers/xrRender*`) что-то — `render-engineer`
- macOS packaging / .mm / CMake Apple-specific — `platform-build`
- Lua VM / luabind / CScriptEngine — `script-engineer`

### Delegate → `team-lead`
- ragnar задаёт стратегический/мета-вопрос («что нам с проектом», «куда дальше», «какой подход»)
- ragnar использует vague references («это», «такая штука», «как мы вчера обсуждали»)
- Foreground Claude ловит себя на 3+ failed fix attempts в одной задаче → **MANDATORY** consilium через `team-lead`
- Cross-cutting decision (нескольких модулей, или architectural pivot)
- Контекст session становится heavy — нужен summarize + park

### Delegate → `Explore` или general-purpose
- Research breadth: «где живёт X», «найди все callers Y» — `Explore`
- Multi-step parallel work без необходимости в специализации — general-purpose

## Тон и формат

### Recommendation-first
- НЕ давать 4-option AskUserQuestion list когда у меня больше данных чем у ragnar'а
- Формат: «Я считаю X, потому что Y. Если не согласен — рассмотри Z.»
- AskUserQuestion ОК когда у ragnar'а действительно есть инсайт которого у меня нет (preference, business context, strategic priority)

### Push back
- Если вижу что направление сомнительное — НЕ молча выполнять
- Озвучить concern, предложить альтернативу, ждать решения
- Memory `feedback_stick_to_plan`: но default — выполнять approved план

### Brief
- 2-3 предложения > параграф > essay > presentation
- Списки и таблицы только когда сравнение реально нужно

### Conversational, не presentation
- Меньше «### Heading» в чате, больше прозы
- Не структурировать каждый ответ как PR description

## Skill discipline

Skills — это не «иногда удобно», это **обязательные gates** для определённых
триггеров. Реinventing «как мы это делаем» каждый раз когда возникает
паттерн — против правила [feedback_scriptify_repeats]. Skills есть для того
чтобы не reinventить — используй их.

### Brainstorming-first gate

**Триггер:** любая creative/feature/новая фича/«а что если». До того как
открывать план или писать код — `Skill superpowers:brainstorming`.

Это значит: до plan, до Issue body, до brainstorming со специалистом —
сначала сам skill. Skill structured exploration целей/требований/edge
cases — без неё план собирается из «легко обозримого», а не из реальной
задачи (см. team-lead push-back в плане 2026-05-24).

### Verification-before-completion gate

**Триггер:** прежде чем сказать «готово» / открыть PR / запросить smoke у
ragnar'а — `Skill superpowers:verification-before-completion`.

Verification ≠ smoke. **Smoke = ragnar играет** (memory `feedback_user_is_primary_tester`).
**Verification = Claude собрал, прогнал, убедился что не сломалось** и
заявляет об этом конкретными результатами. Без verification «готово»
становится «оставил ragnar'у». Это разные дисциплины, нужны обе.

В перспективе verification должен быть скриптом (`make verify` /
`scripts/verify-pre-pr.sh`) — план Issue E (gitea issue для post-D
работы) — чтобы не reinventить чеклист каждый раз.

### Done-criteria activation

**Триггер:** перед закрытием task'а / merge'ом PR / заявлением «1.0
ready» — сверь с [done-criteria.md](done-criteria.md).

Этот файл существует с 2026-05-18 и содержит **формальный shipping
checklist** для macOS 1.0 (functional, quality, distribution,
documentation, upstream backlog, non-goals). До 2026-05-24 он жил в
забвении — никто на него не ссылался. Сейчас он подключён: любой agent,
закрывающий значимый task, читает его до declaration «done».

### Skills triggers table

Какие skills когда вызывать на этом проекте:

| Триггер | Skill | Почему |
|---------|-------|--------|
| Старт сессии в этом репо | `xray-16-engine-work` | Загружает project-specific conventions, code-pointers, log paths. Memory `feedback_invoke_xray_skill_at_start` mandate |
| Creative/feature/«а что если» | `superpowers:brainstorming` | Структурированное exploration до plan |
| Первый bug / repro / «не работает X» | `superpowers:systematic-debugging` | Phase-based debug; phase 4.5 = 3+ failed → STOP, consult team-lead (см. ниже) |
| Multi-step task с >3 шагов | `superpowers:writing-plans` | План отдельным файлом до кода |
| Multi-step execution в этой сессии | `superpowers:executing-plans` | Чеклист step-by-step с verification checkpoints |
| Перед PR / «готово» declaration | `superpowers:verification-before-completion` | Прогон тестов/build, evidence до assertion |
| Перед merge / финальный review | `superpowers:requesting-code-review` | Cross-agent review перед merge |
| 2+ независимых tasks параллельно | `superpowers:dispatching-parallel-agents` | Один tool-block, несколько Agent вызовов |
| Code review PR'а (review-PR команда) | `code-review:code-review` | Project-aware checklist |
| Memory consolidation / archiving | (manual через MEMORY.md edit) | Парковка до отдельной meta-сессии |
| Обновление CLAUDE.md по уроку сессии | `claude-md-management:revise-claude-md` | Targeted CLAUDE.md updates |

Skills не загружаются автоматически (кроме session-start реминдеров) —
вызывай явно через `Skill <name>` tool.

## End-of-session ritual

В конце каждой существенной сессии foreground Claude:

1. **Memory MEMORY.md**: добавить новый memory entry если ragnar выдал durable feedback
2. **`notes/conventions/working-agreement.md`** (этот файл): обновить если эмерджентно появилось новое правило/anti-pattern
3. **`notes/playbooks/gotchas.md`**: добавить если surfaced новый landmine
4. **`notes/reference/engine-map.md`**: обновить если эксплорнул новую часть кода которую сложно было найти
5. **Park open decisions explicitly**: «Открыто: <вопрос>. Файл: <X>. Контекст: <Y>.» — чтобы следующая сессия не теряла state

## Anti-patterns (do NOT do)

| Не делай | Делай вместо |
|----------|--------------|
| Длинный 4-option AskUserQuestion list когда я знаю больше ragnar'а | Recommendation + alternative |
| Молча выполнить сомнительный direction | Push back, propose alternative |
| «Сам быстрее» для 3+ повторяющегося task | Delegate to specialist OR создай skill |
| Hold всё в голове когда контекст heavy | Park в notes/, делегируй state |
| Делать «маленькую» правку сам потому что delegation overhead | >10 LOC = subagent в worktree |
| Запускать subagent без файлов/строк в спеке | Briefing должен быть self-contained с absolute paths |
| Spawn ещё одного team-lead из team-lead | Anti-recursive: team-lead → специалисты, не → team-lead |
| Ship код без smoke от ragnar'а | Никогда. User — primary tester. |

## Когда `team-lead` обязателен

**3+ failed fix attempts** в одной задаче — systematic-debugging skill Phase 4.5 explicitly требует «STOP and question architecture». В нашем формате это значит: дёрнуть `team-lead` для consilium с adversarial cpp-engineer/platform-build, синтезированный verdict, перевыбор approach.

**Контекст session > 100k tokens или > 3 часа работы** — `team-lead` делает summarize + park, чтобы можно было свободно продолжать или закрыть.

## Stable release track

Канал разделён на DEV (`/Applications/OpenXRay-Dev.app`, каждый коммит)
и STABLE (`/Applications/OpenXRay.app`, только after smoke pass).
Команды: `make ship` → DEV, `make promote` → STABLE + git tag
`stable-YYYYMMDD-HHMMSS` (pushed to origin). Откат: `make rollback-stable
TAG=stable-...`. Полное описание + criteria for promote — в
[release-track.md](release-track.md).

Правило для foreground Claude: НЕ запускать `make promote` от лица
ragnar'а. Promote = ragnar'ово решение (он primary tester). Я могу
готовить (assemble PR, прогнать build, дать checklist), он жмёт кнопку.

## Tooling expectations

- **Web search дисциплина**: subagent'ы (cpp-engineer, platform-build, team-lead) умеют WebFetch/WebSearch — перед изобретением workaround'а проверять есть ли OSS precedent. Особенно для платформ-specific багов (macOS, Tahoe).
- **`uv tool install X`** для Python tools, не pip3 (memory `feedback_python_tooling_uv`).
- **Submodule edits** только когда vendored library — abandonware (нет апстрима) или мы держим personal fork (memory `feedback_macos_fork_only`). Документировать в [`notes/decisions/known-divergence.md`](../decisions/known-divergence.md).

## Технические нюансы инструментария

**Новые `.claude/agents/*.md` загружаются ТОЛЬКО при старте сессии.**
Создал нового агента — нужен restart (`/clear` + новая команда, или ctrl-c
+ заново запустить claude). Foreground Claude в текущей сессии не сможет
дёрнуть только что созданного агента. Это поведение системы, не баг.

**Memory hooks `feedback_*`** загружаются автоматически каждую сессию.
Skills тоже доступны через `Skill` tool без restart'а — это только про
agents.

---

## Что НЕ покрыто, возможно стоит добавить позже

- Бюджеты времени per session (когда уйти спать, когда форсировать end-of-session)
- Этикет «consult external Claude Chat» — мы делали раз, было полезно
- Когда обновлять `.git-blame-ignore-revs` (mass reformat commits)
- Automated end-of-session script (план Issue E meta-сессии 2026-05-24 —
  `scripts/end-of-session.sh` walks через 5 шагов ritual)
