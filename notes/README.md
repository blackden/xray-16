# notes/ — карта дерева

Документация форка macOS-OpenXRay. Структурирована по типу контента,
не по хронологии.

## Где что лежит

| Каталог | Что внутри | Кому читать |
|---------|-----------|-------------|
| [`conventions/`](conventions/) | Правила взаимодействия и shipping discipline: working-agreement, sub-agents, done-criteria, release-track, save-format-policy | Любая сессия в начале работы |
| [`reference/`](reference/) | «Где живёт X» индексы и руководства: engine-map, architecture, apple-silicon, macos-build-guide, macos-distribution, cheatsheet, performance-options | Когда нужно понять как устроена подсистема |
| [`playbooks/`](playbooks/) | Известные грабли и процедуры: gotchas, bug-patterns, process-supervision, smoke-tests, issues-playthrough | Когда что-то ломается или нужно прогнать smoke |
| [`decisions/`](decisions/) | ADR-style решения: decisions, known-divergence, save-format-audit, save-format-versioning | Исторический контекст «почему так сделано» |
| [`strategy/`](strategy/) | Forward-looking: roadmap, roadmap-4year, creator-voice, management, mentor-principles, content-system, progress, cpp-tooling-roadmap | Стратегические дискуссии, не операционные |
| [`archive/2026-05/`](archive/) | Старые session dumps и устаревшие заметки | Историческая справка, обычно не нужно |

## Куда писать новое

| Тип записи | Куда |
|-----------|------|
| Новое правило работы / anti-pattern / процесс | `conventions/working-agreement.md` (как секция) |
| Новый file:line указатель в коде | `reference/engine-map.md` |
| Новая грабля / landmine которую я нашёл и не хочу разоблачать снова | `playbooks/gotchas.md` |
| ADR-style решение «почему так, а не иначе» | `decisions/decisions.md` |
| Долгосрочная мысль / direction / «а что если» | `strategy/creator-voice.md` (creator) или `strategy/roadmap-4year.md` (strategic arc) |
| Workflow lesson из ретроспективы сессии | `strategy/management.md` |
| Принципы общения mentor↔ragnar (как меня вести / как мне тебя пушить) | `strategy/mentor-principles.md` |
| Контент-стратегия (видео, Telegram, монетизация) | `strategy/content-system.md` |
| Внутренний tooling roadmap (CI / linters / hooks) | `strategy/cpp-tooling-roadmap.md` или `conventions/scripts-and-automation.md` (когда появится в Issue E) |

Если не уверен куда — спроси `team-lead` агента, он подскажет.

## Принципы

- **Один файл — одна тема.** Когда файл вырастает в junk-drawer (как
  это было с `engine-thoughts.md` до 2026-05-24) — split по топикам,
  не по хронологии.
- **Cross-refs относительно текущего файла.** Из `conventions/` в
  `reference/` — пиши `[engine-map.md](../reference/engine-map.md)`.
- **Архивируй, не удаляй.** Старые session-dumps и устаревшие документы
  переезжают в `archive/<YYYY-MM>/`. Архив не нужно открывать — он
  существует для исторического контекста.
- **README этого каталога — индекс, не контент.** Если хочешь добавить
  правило / факт — это идёт в соответствующий файл по таксономии, а не
  сюда.

## Связано (за пределами notes/)

- `CLAUDE.md` (корень репо) — project-wide rules для агентов.
- `.claude/agents/*.md` — определения специализированных агентов.
- `~/.claude/projects/-Users-ragnar-fedorov-tech-xray-16/memory/` —
  персональная память Claude между сессиями (feedback rules, durable
  preferences).
- `~/.claude/skills/xray-16-engine-work/` — project-specific skill,
  загружаемый в начале сессии.

## История реорганизаций

- **2026-05-24** — первый reorg плоского `notes/` в дерево по
  таксономии. Issue #83 / PR. Параллельно: split
  `engine-thoughts.md` (51KB) на три топических файла, slim
  `sub-agents.md` (убрана duplication с working-agreement.md),
  governance update в working-agreement.md (skill discipline gates),
  archive 14 session-202605* директорий.
