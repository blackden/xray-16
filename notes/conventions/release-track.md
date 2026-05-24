# Release track — DEV vs STABLE

Personal macOS fork. Two release channels, both на одной длинной ветке
`macos/blackden/master`. Channel = просто куда установлено, отдельных
веток для stable нет. Stable worktree `macos/stable` всегда
fast-forward'нут до конкретного коммита master'а, помеченного тегом
`stable-YYYYMMDD-HHMMSS`.

## Каналы

| Канал | App bundle | Branch / worktree | Команда | Когда |
|-------|-----------|-------------------|---------|-------|
| DEV | `/Applications/OpenXRay-Dev.app` | `macos/blackden/master` (текущий) | `make ship` | Каждый коммит / итерация |
| STABLE | `/Applications/OpenXRay.app` | `macos/stable` worktree в `../xray-16-stable` | `make promote` | Только после smoke pass |

`make ship` собирается из текущего checkout'а и кладётся в DEV. Это рабочий
канал для итерации — может ломаться, может быть наполовину готов. Ragnar
играет в DEV когда тестирует свежую правку.

`make promote` берёт текущий HEAD (мaster), fast-forward'ит stable
worktree, собирает там же и кладёт в STABLE — **только** если smoke-чеклист
прошёл. Создаёт git tag `stable-YYYYMMDD-HHMMSS` и пушит на origin (gitea).
STABLE — то, во что ragnar играет когда мы что-то ломаем в DEV.

## Когда promote (criteria)

Не promote'им «потому что давно не было». Promote'им когда конкретный
зелёный commit пережил:

1. Build: `make build-release` без warnings из новых файлов.
2. Boot: `make ship` запускается, доходит до главного меню, не ловит
   safe-mode sentinel.
3. Save/Load: загрузить сохранение CoP, поиграть 5 минут, выйти Cmd+Q,
   перезапустить, загрузить тот же save. Нет крашей, нет потери данных.
4. (Если правка трогает критические системы — рендер/звук/физика/AI) —
   полный smoke по `notes/playbooks/updater-smoke.md` (или релевантный).

После этого: `make promote`. Tag создастся автоматически.

## Как откатить

Stable сбился, новая promote'нутая версия крашится, нужно откатиться к
предыдущей stable:

```bash
make list-stable-tags                          # последние 20 тегов
make rollback-stable TAG=stable-20260524-1234  # выбрать конкретный
```

Под капотом: stable worktree переключается на detached checkout этого тега,
бинарь пересобирается, кладётся в `/Applications/OpenXRay.app`. После
этого ragnar может играть в стабильную старую версию пока чиним
проблему в DEV. Когда починили — следующий `make promote` снова
fast-forward'ит stable до HEAD (это re-attach worktree из detached
state).

## Что не входит

- Auto-promote по green CI — нет, ручной trigger всегда. CI на форке нет
  как такового; primary tester — ragnar.
- Stable не имеет своей долгоживущей ветки с независимым жизненным циклом.
  Это просто «текущий promoted commit master'а», помеченный tag'ом.
- DMG/notarization/Sparkle auto-update — отдельная тема, не часть этого
  потока. Stable bundle подписан ad-hoc (см. `scripts/mac/package_app.sh`),
  notarization придёт с native rewrite.

## Связано

- `Makefile:359-401` — DEV_APP_DIR, STABLE_APP_DIR, STABLE_WORKTREE,
  ship/promote/list-stable-tags/rollback-stable targets.
- `scripts/mac/package_app.sh` — собирает .app bundle, PRODUCT_NAME
  параметризован (по умолчанию OpenXRay).
- `working-agreement.md` § Stable release track (same dir) — краткое
  summary для агентов.
