# Scripts & automation — how rules are enforced

Per memory `feedback_scriptify_repeats`: повторяющиеся checklists должны
быть в скриптах, не в дисциплине. Этот файл — карта: какое правило
[working-agreement.md](working-agreement.md) автоматизировано какой
командой / hook'ом / skill'ом.

Связано: [working-agreement.md](working-agreement.md) (rules),
[release-track.md](release-track.md) (DEV/STABLE channels),
[done-criteria.md](done-criteria.md) (gate для 1.0).

## Map: rule → automation

| Rule из working-agreement.md | Где автоматизировано | Запуск |
|------------------------------|----------------------|--------|
| Stylecheck (encoding/newline/whitespace) на push | `scripts/git-hooks/pre-push` Check 2 | `make install-hooks` один раз, дальше каждый push |
| Build не сломан на push | `scripts/git-hooks/pre-push` Check 3 | Авто на push (skipped если no code changes) |
| Нет dead notes/ refs на push | `scripts/git-hooks/pre-push` Check 4 → `scripts/check-doc-refs.sh` | Авто на push (skipped если no doc-relevant changes) |
| Auto-ship DEV на commit | `scripts/git-hooks/post-commit` | `make install-hooks` один раз |
| Pre-PR sanity (refs + status + clang-format) | `make verify` | Запускается вручную перед opening PR |
| End-of-session ritual (5 шагов) | `make end-of-session` или `./scripts/end-of-session.sh` | Foreground Claude в конце significant session |
| Stable promote + git tag | `make promote` | Ручной trigger ragnar'ом после smoke pass |
| Stable rollback | `make rollback-stable TAG=stable-...` | Ручной trigger при regression |
| Skills triggers (brainstorming / verification / etc.) | Skill discipline в working-agreement.md § «Skill discipline» | Claude явно вызывает `Skill <name>` на соответствующих gates |
| Issue-driven workflow | gitea MCP + branch naming convention | Foreground Claude следует pattern; не автоматизировано |

## Скрипты

### `scripts/check-doc-refs.sh`

Walks all `notes/<subtree>/<file>.md` refs across репо, exits non-zero
на dead refs. Allowlist в `scripts/doc-refs-allowlist`.

```bash
./scripts/check-doc-refs.sh            # standalone
make verify                             # combined sanity
# Auto: pre-push hook Check 4
```

Какие refs ловит: только absolute-style `notes/...md`. Relative refs
внутри notes/ (типа `../reference/foo.md`) — out of scope.

Что добавить в allowlist: только заведомо-непустые placeholders
(`notes/session-N.md`, etc.) или ссылки на файлы которые будут созданы
позже.

### `scripts/end-of-session.sh`

Interactive walk через 5 EoS ritual steps из working-agreement.md:
MEMORY / working-agreement / gotchas / engine-map / parked decisions.
Prompts y/n/s, summarises pending.

```bash
./scripts/end-of-session.sh             # standalone
make end-of-session                     # via Makefile
```

Не идемпотентен — рассчитан на interactive вызов foreground Claude в
конце session.

### `make verify`

Pre-PR sanity. Делает:
1. `scripts/check-doc-refs.sh` (must pass)
2. `git status` (informational warning если dirty)
3. clang-format dry-run на staged `*.{cpp,h,hpp,cxx,c,cc,mm,m}` (must pass)

Запускается foreground Claude или ragnar'ом перед `git push`. Дополняет
pre-push hook, не дублирует — verify проверяет staging state, hook
проверяет push range.

### `make promote` / `make list-stable-tags` / `make rollback-stable`

Stable release track (см. [release-track.md](release-track.md)).

### `scripts/git-hooks/{pre-push,post-commit,post-merge}`

Хуки. Активируются через `make install-hooks` (один раз).

- **pre-push**: 4 проверки (commit subjects / whitespace+newline / build /
  doc-refs). Override: `OPENXRAY_SKIP_PREPUSH=1 git push`
- **post-commit**: auto-ship DEV на каждый commit (skipped если
  docs-only)
- **post-merge**: TBD

## Принципы

- **Real-not-shelf** (memory `feedback_tooling_real_not_shelf`): каждый
  скрипт здесь имеет конкретный use case в активном workflow. Если
  что-то перестало использоваться — удалить, не оставлять «может
  пригодится».
- **Portable shell**: bash, POSIX-compatible. Не использовать
  ripgrep/jq/uv в скриптах если можно обойтись grep/awk/curl. (rg —
  shell-function в текущей сессии Claude, не binary, поэтому subshell
  его не видит).
- **Fail loud**: скрипты exit non-zero на ошибки, печатают actionable
  hint в stderr. Никаких silent failures.
- **Discoverable**: каждый скрипт упомянут здесь + в `make help`. Если
  нет — discovery chain сломан (memory rule).

## Не покрыто (park до future meta-session)

- `scripts/smoke-checklist.sh` — interactive walk через smoke steps
  для конкретной фичи. Зависит от формализованного smoke-checklist
  формата которого ещё нет.
- Automatic memory consolidation tooling.
- Link-checker для external URLs в docs.
- CI-side enforcement тех же checks (текущий CI только stylecheck).
