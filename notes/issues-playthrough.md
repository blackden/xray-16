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

## 2026-05-19 — Фонарик не освещает поверхности на Apple GL (P2)

Игрок сообщает: light от фонарика не падает на surfaces (не на все).
Не приложил скриншот. Похоже на known long-standing GL-renderer gap —
дополнительные dynamic source-attached lights stubbed на OGL-пути в
`xrRender_R2` / `xrRenderPC_GL`. Семейство со SSAO bug — `accum_omni_*`
шейдеры (omni light = точечный источник, фонарик именно такой).

**Гипотеза:** `accum_omni_normal_nomsaa.ps` и `accum_omni_transluent_nomsaa.ps`
тоже в shader compile-failure cascade (видно в логе того же дня).
После SSAO gate cascade не запускается — но если фонарик использует
permutation который compile-failed *по другой причине* (например
`ssao_blur_on` в omni light path), он по-прежнему молча падает на GL.

**Что проверить, когда дойдём:**
- В логе свежей сессии после SSAO-gate коммита: остался ли cascade
  на `accum_omni_*`? Если нет — фонарик должен заработать.
- Если cascade ушёл, но фонарик всё равно не светит → искать в
  `src/Layers/xrRender_R2/Light_Package.cpp` или `light.cpp`
  специфичный USE_OGL гейт который stub'ит omni shading.
- `notes/engine-map.md` секция "Open questions" уже содержит этот
  пункт — обновить когда разберёмся.

**Severity:** P2 — заметная geometry deficiency, но играть можно.

---

## 2026-05-19 — NPC в Yanov залипли в combat-pose (P2, self-resolved)

**Resolved через chunk-eviction.** Игрок отошёл далеко по другому
заданию → smart_terrain `jup_a6` выгрузился из ALife working set →
при возврате NPC re-initialized с чистым job assignment, баг ушёл.

Корень не исследован, но воспроизведение разовое. Если повторится в
другой smart_terrain — открываем заново. Перевожу с P1 на **P2 наблюдение**.

**Что осталось в backlog для следующего encountera:**
- `src/xrServerEntities/script_object_inventory.cpp` /
  `smart_terrain.script` — где назначение job происходит. На load
  возможно race между smart_terrain restore и NPC scheduler init.
- Лог-тэги для grep: `smart_terrain.*restore`, `job.*assign`,
  `combat.*lost`.

---

## 2026-05-19 — NPC в Yanov залипли в combat-pose (P1, **original entry**)

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

## 2026-05-19 — SSAO mode → пропадает вся отрисовка уровня (P1, **closed**)

**Корневая причина:** `src/Layers/xrRenderPC_GL/rgl_shaders.cpp`
определял макрос `SSAO_QUALITY` (и `SSR_QUALITY`, `SUN_SHAFTS_QUALITY`,
`SUN_QUALITY`) **только когда соответствующая фича была включена**.
Когда выключена — макрос вообще не существовал, а шейдеры используют
его в `#if SSAO_QUALITY > 3` / `#if SSAO_QUALITY <= 3`. По C99
undefined identifier в `#if` → 0, но Apple GL 4.10 GLSL parser strict
и выдаёт «incorrect preprocessor directive», ломая весь deferred-
pipeline.

**Fix:** unconditional `options.add("SSAO_QUALITY", "0")` когда фича
off. Аналогично для трёх соседних *_QUALITY-макросов. Это safer-by-
default: на остальных GL-платформах ничего не меняется (`#if 0 > 3`
эквивалентно undefined-rule), но Apple GL теперь счастлив.

**Параллельно:** убран temp-gate из `xrRender_R2/r2.cpp:381-405`
(commit d7724e2c9), возвращён vanilla SSAO path.

**Статус:** real-fix landed. Уровень с включённым SSAO рендерится на
Apple GL без cascade.

---

## Исходная запись (для истории)

**Repro:** в опциях графики переключить SSAO с "Disabled" на любой
включённый режим (HDAO/HBAO/Standard) → применить → нужна перезагрузка
карты → после перезагрузки видны **только костры, HUD, дождь, скайбокс**;
вся geometry уровня черно-серая (G-buffer не композится).

Возврат на "Disabled" + перезагрузка карты → всё восстанавливается.
**Игра не вылетает**, но фактически unplayable в этом состоянии.

**Корень в логе (`~/Library/Logs/OpenXRay/openxray.log`):**

```
! shader compilation failed: gl\accum_sun_nomsaa.ps\125601111001100000000000010000013111103001000000000
! error:  ERROR: 0:4483: '' : syntax error: incorrect preprocessor directive
! error:           syntax error: unexpected tokens following #if preprocessor directive
```

Каскад на 8+ шейдеров: `accum_sun_*`, `accum_sun_near_*_minmax`,
`accum_volumetric_sun_*`, `rain_layer`, `rain_patch_normal_*`,
`accum_omni_normal_*`, `accum_omni_transluent_*`, `combine_1_nomsaa`.

Permutation hash: `12560`...`131111`**03**`001`... (chunk `03` указывает на
SSAO_QUALITY/MODE компонент permutation key). После retry chunk
становится `00` — но всё равно fail. Apple GL 4.1 (Metal-backed)
GLSL compiler ломается на `#if SSAO_QUALITY > 3` или подобной
директиве в SSAO include-цепочке (видно в дампе шейдера: комментарий
`// xxx: disabled error in "ssao_hdao_new.ps" for msaa`).

**Дополнительные осложнения:**
- Параллельно `glFramebufferTexture2D -> 0x502` — known Apple GL VAO/RT
  issue, **отдельный фронт работ** (мы уже фиксили в Phase 3 UTF-8
  saga, но не на всех путях).
- После shader compile fail engine входит в retry-loop каждый кадр и
  становится TX (traced-stopped) — единственный recovery через Force
  Quit + safe-mode sentinel.

**Что чинить (по приоритету):**

1. **Cheap fix (1 commit, P1):** на старте, после load `user.ltx`,
   запустить tryCompile для shader-variants, которые triggers SSAO modes
   на Apple GL. Если хоть один fails — silently force `r2_ssao = off`
   до перезапуска и логнуть warning. Не даёт пользователю выстрелить
   себе в ногу. Patch-точка: `src/Layers/xrRender_GL/glHW.cpp` post-init,
   или в самом options-screen apply-handler.

2. **Real fix (1-2 дня, P2):** найти конкретную `#if` директиву в
   SSAO include-цепочке, которая ломает Apple GLSL parser. Скорее всего
   многострочный `#if defined(USE_HDAO) && SSAO_QUALITY > 3` без
   continuation — Apple GL глотает это иначе чем NVIDIA/AMD. Файлы:
   `gamedata/shaders/gl/ssao_*.ps` и `accum_sun_cascade.ps` include
   chain. Требует unpack `resources.db?`.

3. **Defensive (опционально, P3):** UI options screen → опции `HBAO`/`HDAO`
   надо помечать как "experimental / may not work on Apple GL" или
   просто скрыть на платформе, где tryCompile зафейлил.

**Severity:** P1. Прямой quest-blocker: настройка из UI оставляет
игрока в неиграбельном состоянии **навсегда** до ручного recovery
через файлы. Это не cosmetic regression, это feature, который
выглядит рабочим, но ломает игру.

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
