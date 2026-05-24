# Working agreement — ragnar × Claude × subagent team

Зачем этот документ: за пару недель плотной работы мы накопили рабочие
паттерны, но они жили в моей (Claude) голове и в эпизодических feedback'ах
от ragnar'а. Это привело к тому что когда контекст разрастается, я
перестаю надёжно их применять. Этот файл — явный rulebook, к которому
все агенты обращаются. Обновляется в конце каждой сессии если что-то
новое выкристаллизовалось.

Связано: [sub-agents.md](sub-agents.md) (когда дёргать какого
специалиста), [engine-map.md](engine-map.md) (где живёт что в коде),
[gotchas.md](gotchas.md) (грабли), [engine-thoughts.md](engine-thoughts.md)
(стратегические записи), `MEMORY.md` (durable user preferences),
`.claude/agents/*` (роли).

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

## End-of-session ritual

В конце каждой существенной сессии foreground Claude:

1. **Memory MEMORY.md**: добавить новый memory entry если ragnar выдал durable feedback
2. **`notes/working-agreement.md`** (этот файл): обновить если эмерджентно появилось новое правило/anti-pattern
3. **`notes/gotchas.md`**: добавить если surfaced новый landmine
4. **`notes/engine-map.md`**: обновить если эксплорнул новую часть кода которую сложно было найти
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

## Tooling expectations

- **Web search дисциплина**: subagent'ы (cpp-engineer, platform-build, team-lead) умеют WebFetch/WebSearch — перед изобретением workaround'а проверять есть ли OSS precedent. Особенно для платформ-specific багов (macOS, Tahoe).
- **`uv tool install X`** для Python tools, не pip3 (memory `feedback_python_tooling_uv`).
- **Submodule edits** только когда vendored library — abandonware (нет апстрима) или мы держим personal fork (memory `feedback_macos_fork_only`). Документировать в `notes/known-divergence.md`.

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

- Конкретные триггеры для `Skill` invocation (которые skills всегда, которые контекстуально)
- Бюджеты времени per session (когда уйти спать, когда форсировать end-of-session)
- Этикет «consult external Claude Chat» — мы делали раз, было полезно
- Когда обновлять `.git-blame-ignore-revs` (mass reformat commits)
