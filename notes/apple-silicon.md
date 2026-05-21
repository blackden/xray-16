# Apple Silicon (macOS ARM64) — план разбора

Симптомы у пользователя на M-серии Mac:
- вылеты при заходе в игру
- после некоторых действий пропадают модели оружия, земли, окружения (как будто не загружены)
- при приближении к NPC — вылет

В `ReleaseMasterGold` (стандарт). На x86_64 macOS/Linux таких проблем нет.

## Состояние поддержки в репо

- README заявляет ARM64/macOS как поддерживаемые, без оговорок.
- CI (`.github/workflows/cibuild.yml`) **собирает** `macos-latest arm64`, но **не запускает** игру — зелёный билд ≠ играбельно.
- Контрибьюторы по macOS есть (`vertver`, `Lnd-stoL`), но направление явно недотестировано.

## Подозрительные места в коде

1. **FPU control на ARM64 — no-op.** `src/xrCore/_math.cpp:96-101` — макросы `_MM_SET_FLUSH_ZERO_MODE` и `_MM_SET_DENORMALS_ZERO_MODE` определены пустыми для ARM/ARM64/PPC64. На x86 движок включает FZ/DN, а на ARM никто не выставил эквивалентные биты FPCR (FZ — бит 24, DN — бит 25). Возможный источник NaN-каскада → пропадающая геометрия/анимация.

2. **`sse2neon`-эмуляция SSE на ARM** — в горячем коде:
   - `src/xrCDB/xrCDB_ray.cpp`, `src/xrCDB/ISpatial_q_ray.cpp` — рейкасты/коллизии.
   - `src/Layers/xrRender/ParticleEffect.cpp`, `src/Layers/xrRender/DetailManager.cpp` — рендер деталей/партиклов.
   - `src/xrParticles/noise.cpp` — Перлин-шум.
   Часть путей принудительно скалярная (`ParticleEffect.cpp:467-477` — `FillSprite_fpu`).

3. **`PlatformApple.inl`** — фактически копия Linux-варианта, без macOS-специфичных оптимизаций. Один открытый `// FIXME-VIEWPORT` в `src/xrEngine/Device_mode.cpp` про DPI (не связано с моделями).

## План действий, когда вернёшься

1. **Пересобрать `Mixed` или `Debug`** вместо `ReleaseMasterGold`:
   ```bash
   cmake -B build -DCMAKE_BUILD_TYPE=Mixed
   cmake --build build -j
   ```
   В дебаге сработают assert'ы — точка падения станет осмысленной, а не «где-то в рендере».

2. **Убедиться, что бинарь arm64-нативный, не Rosetta:**
   ```bash
   file build/bin/.../xr_3da    # ожидаем Mach-O 64-bit executable arm64
   ```

3. **Собрать crash report:** `~/Library/Logs/DiagnosticReports/*.ips` — там виден модуль и стэк (`xrCDB` / `xrGame` / `xrRender_GL` — разные диагнозы).

4. **Лог движка:** запускать из терминала, чтобы видеть stdout; полный лог — рядом с `fsgame.ltx` (`logs/`).

5. **Поискать на GitHub:** [issues OpenXRay/xray-16](https://github.com/OpenXRay/xray-16/issues) по запросам `macos arm64`, `apple silicon`, `m1`, `m2`. Если репро уже описан — добавить туда свой crash report, не плодить дубль.

6. **Быстрый эксперимент**, который может починить «пропадающие модели»: в `_initialize_cpu_thread()` (`src/xrCore/_math.cpp`) для ARM64 явно выставить FZ/DN через FPCR:
   ```cpp
   #if defined(XR_ARCHITECTURE_ARM64)
       uint64_t fpcr;
       __asm__ __volatile__("mrs %0, fpcr" : "=r"(fpcr));
       fpcr |= (1ULL << 24) | (1ULL << 25);  // FZ | DN
       __asm__ __volatile__("msr fpcr, %0" :: "r"(fpcr));
   #endif
   ```
   Если помогает — это готовый PR в апстрим.

7. **Собрать с `-DXRAY_USE_ASAN=ON`** — AddressSanitizer на arm64-macOS ловит UB/use-after-free, который на x86 случайно «работает».

## Что нужно собрать перед возвратом к задаче

- модель Mac + версия macOS
- какая игра (CoP/CS) и какая версия gamedata
- `file` на бинарь
- последние ~50 строк лога движка перед падением
- crash report `.ips` (имя модуля + первые ~10 фреймов стэка)
- репро-сценарий (как именно — заход в игру / приближение к NPC / какая локация)
