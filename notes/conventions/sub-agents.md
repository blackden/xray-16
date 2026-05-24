# Sub-agents — dispatch playbook

Детальный playbook для делегирования агентам. Высокоуровневые правила
«когда какой агент» — в [working-agreement.md](working-agreement.md);
здесь — конкретика briefing'а и parallelism'а.

Зачем: главный контекст в этой репе разъедается двумя вещами — большие
grep-результаты по `xr*/` и длинные дампы из `src/Layers/`. Sub-agent
отрабатывает запрос в своём контексте и возвращает только summary —
главный остаётся ёмким, мы дольше живём до auto-compact.

## Signal → action

| Сигнал | Действие |
|---|---|
| «Где определена/вызывается функция X» — нужен 1 grep | **`Bash grep`** напрямую, не агента |
| «Где определена X» — нужны 2-3 поиска (имя файла + grep + cross-ref) | **`Bash grep`** напрямую |
| «Где-то в `xrEngine/` есть pattern Y, найди все callsite-ы» — 3+ запросов | **`Agent subagent_type=Explore breadth=medium`** |
| «Как устроен модуль Z» (например, ALife loader) — нужна структурная картина | **`Agent subagent_type=Explore breadth=very thorough`** или **`feature-dev:code-explorer`** |
| «Спроектируй фикс/feature X с учётом конвенций» | **`Agent subagent_type=Plan`** или **`feature-dev:code-architect`** |
| Параллельные независимые задачи (упаковка + lint + smoke-test) | Несколько `Agent` в одном tool-блоке (parallel) |
| C++ lifecycle / destructor / RAII / signal-handler audit | **`cpp-engineer`** (memory `feedback_delegate_cpp_analysis`) |
| Render layer (`src/Layers/xrRender*`) | **`render-engineer`** |
| Платформенно-специфичная упаковка/CMake Apple/launcher/Cocoa/AppKit/notarization | **`apple-platform`** агент |
| Lua VM / luabind / CScriptEngine | **`script-engineer`** |
| Strategic/meta-вопрос или 3+ failed fix attempts | **`team-lead`** (см. working-agreement.md) |
| Code review на ветке | **`code-review:code-review`** скилл (его собственный поток) |
| Security pass перед PR в апстрим | **`code-modernization:security-auditor`** |

## Когда **НЕ** делегировать

- Один прицельный grep, одно открытие файла — Read/Grep сами по себе быстрее.
- Правка кода, которая уже понятна, — агент только добавит туда-сюда.
- Решения, требующие user-context-а (что мы хотим в продукте) — это моё, не агента.
- Безопасные действия с побочкой (commit, push, gh) — делаем сами, чтобы видеть.

## Шаблон брифинга (mandatory format)

Агент не видит нашу историю. Хороший prompt включает:

1. **Цель** — что ищем/строим и зачем (1-2 строки).
2. **Контекст** — что уже исключили, что уже пробовали, релевантные файлы/коммиты.
3. **Конкретика** — пути (абсолютные), символы, флаги CMake, тестовые сценарии.
4. **Ожидаемый формат отчёта** — «punch-list», «таблица», «коротко под 200 слов».

Пример (плохо):
> найди где живёт автосейв

Пример (хорошо):
> Цель: найти все точки где xrGame инициирует save (auto и manual), чтобы
> добавить логирование пути и размера до `FS.w_open`.
> Уже знаем: `alife_storage_manager::save` — один из путей; `autosave_manager.cpp`
> имеет ранний `if (true) return;`. Ищи другие callsite-ы `M_SAVE_GAME` и
> `CALifeStorageManager::save`.
> Отчёт: punch-list файл:строка + 1-строчное описание триггера. Под 150 слов.

## Anti-patterns (parallelism-specific)

- **Делегировать-и-параллельно-самому-искать.** Если запустил Explore — не дублируй
  его работу `Bash grep`-ом в main. Дождись результата.
- **Передавать «исправь баг по своим findings».** Синтез — моя работа. Агенту
  передаём узкие задачи; интеграцию делаю сам.
- **Закидывать в агента «изучи весь модуль xrGame».** Без фокуса вернёт обзор «обо
  всём и ни о чём». Сужай scope до конкретной подсистемы.
- **Запускать тяжёлый Explore до того, как сформулирована гипотеза.** Сначала
  гипотеза → потом таргетированный поиск.

## Параллельный диспатч

Если задачи независимы (нет shared state, нет sequential dependency) — одним
tool-блоком сразу несколько `Agent`. Например, перед PR в апстрим:

- `code-modernization:security-auditor` по diff-у
- `pr-review-toolkit:code-reviewer` по той же диффе
- `Explore` «есть ли upstream-PR, уже трогавший эти файлы»

Запускаются параллельно, отчёты приходят раздельно. Skill
`superpowers:dispatching-parallel-agents` — formal trigger для этого
паттерна.

## Связанные

- [`working-agreement.md`](working-agreement.md) — каноничные правила
  кто/когда/зачем (rules); этот файл — как (mechanics).
- [`../reference/engine-map.md`](../reference/engine-map.md) — куда
  направлять Explore-агентов.
- [`../playbooks/gotchas.md`](../playbooks/gotchas.md) — что агенту проще
  не открывать заново.
- skill `xray-16-engine-work` — кодстайл и code-pointer index, агентам
  можно передавать ссылкой.
