# Playthrough issues — CoP main quest, 2026-05-19

Issues observed during in-engine playthrough of vanilla CoP on Mac
(ReleaseMasterGold, branch `macos/blackden/master`). Recorded as the
user encounters them; triaged later.

## Severity legend

- **P0** — crash, save corruption, blocks progression.
- **P1** — visual regression from vanilla / data loss / quest-blocker
  with workaround.
- **P2** — cosmetic / playable but noticeably wrong.
- **P3** — nice-to-have / vanilla-compatible quirk.

---

## 2026-05-19 — Yanov station, light bloom (P2)

Внутри помещения станции арочные окна выжигают всю стену в почти
чисто-белый бликующий тон — bloom уходит за разумные пределы. Скрин:
`screenshots/2026-05-19-yanov-bloom.png` (если будет сохранён).

**Гипотеза:** разница tonemap-curve в GL рендерере vs DX11. Мы не
трогали bloom path — это **известная разница GL backend** в OpenXRay,
не наш регресс. Игрок временно понизил уровень света в настройках.

**Что проверить:**
- `src/Layers/xrRender_GL/`, шейдеры `s_bloom*` — сравнить логику с
  `xrRender_R2`/`xrRenderDX11`.
- Cvar tuning: `r2_tonemap`, `r2_tonemap_amount`, `r2_tonemap_adaptation`.
- Возможно есть `r__gl_overexposure` или подобный workaround.

**Severity:** P2 (cosmetic, playable). Не для текущей сессии.

---

## 2026-05-19 — Дождь сквозь крышу + wet-shader на внутренних стенах (P2)

В помещении (Yanov station) видны streaks дождя как будто крыша
прозрачная; стены имеют wet/mossy наложение даже на сухих внутренних
поверхностях.

**Корень — vanilla bug, не наш регресс:**
- `src/xrEngine/Rain.cpp:130` — `factor = CurrentEnv.rain_density`
  глобальный, **никакого indoor-check нет**.
- `hemi_factor` (Rain.cpp:131-148) считывает hemi-cube вокруг камеры,
  но используется только для визуальной интенсивности drops, **не для
  сatting эмиссии**. Когда камера в плохо-defined indoor zone (Yanov
  такая по канону — частичная крыша), hemi_factor падает, но дождь
  всё равно рендерится.
- Wet-surface шейдер в G-buffer использует тот же `rain_density` без
  sector/portal-теста.

**Реальный фикс (отдельный поход, ~1-2 дня):**
- Portal/sector-aware rain emission: при `hemi_factor < EPS` skip
  rain streak emission целиком (не просто dim).
- Wet-shader: gate на тот же `hemi_factor < threshold` через G-buffer
  bit или сcaling в pixel shader.

**Severity:** P2. Vanilla CoP делает то же самое. Upstream-кандидат
для PR.

---

## 2026-05-19 — NPC в Yanov залипли в combat-pose (P1)

При входе на станцию Yanov 6 сталкеров стояли в стрелковой позе с
поднятым оружием, целились в одну точку (на самого игрока). **Не
стреляли** — то есть, это **animation/AI state lock**, не faction
shift на врага.

**Гипотезы (по убыванию вероятности):**

1. **Smart-terrain state corrupted после save/load.** NPC не получили
   job assignment после relocation в smart_terrain `jup_a6` — default
   "alarm" state без assigned cover/idle = combat-ready pose.
2. **Stale combat target.** Прошлая боевая ситуация (комбат-уровень
   ~50+ при подходе) оставила target reference на ID игрока, но
   relations не подняла до enemy — поэтому "целятся, не стреляют".
3. **Async ALife update race.** При load save через `M_LOAD_GAME`
   smart_terrain restore приходит раньше, чем NPC schedulers — окно,
   в котором NPC в `unknown` job.

**Что попробовать в-сессии:**
- Выйти наружу за triggering radius smart_terrain и вернуться —
  большинство залипаний снимается re-entry job assignment.
- Если нет — `g_god 1` пройти сквозь них, проверить стреляют ли.
- Дать ALife тиков: `~` → `time_factor 50` → подождать 30 секунд →
  `time_factor 1`.

**Что в логах смотреть** (после следующего такого случая):
```bash
grep -iE 'smart_terrain.*jup|job.*reset|combat.*lost' \
    ~/Library/Logs/OpenXRay/openxray.log
```

**Severity:** P1 если воспроизводится → quest-blocker (нельзя сдать
квесты Бороде). P2 если разовое (script race на load).

---

## 2026-05-19 — Pause-exploit для золотой рыбки у Медведа (P3, exploit-restoration)

Не баг — **пропавший эксплоит**. Vanilla CoP позволял farming артефакта
"Золотая рыбка" у Медведа на Янове через дублирование reward'a:

1. Диалог с Медведом → ветка "давай артефакт за сталкера".
2. Прожать выбор.
3. Пауза.
4. Esc → закрывается диалог.
5. Esc → меню → выход.
6. Interact с Медведом → диалог стартует с того же узла →
   give-item фиксится повторно.
7. Loop.

**Механика бага (гипотеза, требует unpack `.db`):**

Vanilla скрипт не ставит `give_info "med_artefact_given"` после первой
выдачи; ветка диалога остаётся reachable без precondition. Pause-stack
ломает state-машину диалога — на выходе из меню scripts реальная
позиция в диалог-tree не обновляется, и при `interact` дерево
re-entries с прошлой ноды.

**В OpenXRay мы скрипты квестов не трогали** — все engine-fixes
касаются FS / encoding / GL / debug. Так что если эксплоит "сломался"
по факту проверки игроком — нужно тестить ещё раз с rebound pause-key
(`kP`). Скорее всего работает как раньше.

**Если действительно не работает** — нужно:
1. Извлечь `dialogs_jupiter.script` и `bind_logic.script` из
   `resources/resources.db?` (xrCompress -unpack).
2. Найти узел Medved'а и give-action.
3. Сравнить с дампом vanilla 1.6.02 — что изменилось.

Текущий статус: **не воспроизведено игроком после rebind**. Возможно,
просто не пробовали в сессии.

---

## Шаблон для новых записей

```
## YYYY-MM-DD — <место>, <одна строка summary> (P0/P1/P2/P3)

Что видно: ...

Гипотеза: ...

Что проверить: ...

Что в логах: `grep ...`

Severity: ...
```
