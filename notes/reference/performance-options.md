# Acceleration / caching hacks — что разбирали и решения

Зафиксировано 2026-05-18 после вопроса «можем ли мы применять ramfs/tmpfs/Redis/БД для сэйвов или каких-то горячих файлов».

## Реальные узкие места OpenXRay на M1 (по убыванию боли)

1. **GPU TX-state hang** на max settings (compressed 3D textures, MSAA framebuffer attachments). Kernel-level hold, никакая userspace-обвязка не помогает. Лечится только устранением операций, ввергающих в TX — long-term Vulkan/Metal renderer.
2. **Apple GL occlusion queries** (был блокирующий `glGetQueryObjectiv` — мы починили в `8e86e47cd`, `QueryHelper.h` теперь polling без stalls).
3. **Lua bulk load** на старте уровня (~70 скриптов компилируются последовательно). Single-threaded LuaJIT.
4. **Spawn registry + alife objects load** (~11k объектов). CPU + memory bound.
5. **cp1251 filename → EILSEQ автосейвов** — encoding boundary issue, чиним сейчас в `alife_storage_manager`.
6. **`.scop` save write** (~600KB compressed, ~5-15ms на NVMe) — НЕ узкое место.

I/O в этом списке нет. SSD APFS пишет сейвы за миллисекунды. Загружает level data за секунды, но это парсинг, не disk read.

## Что разбирали

### ✅ Стоит делать

**1. OS page-cache pre-warm** (нулевой код):
```bash
# Прогреть gamedata в unified page cache перед запуском
find ~/Games/STALKER-CoP/gamedata/levels -type f -exec cat {} + > /dev/null 2>&1
```
Помогает только cold-start, бесполезно для горячего перезапуска (XNU и так держит). Добавить в Makefile target `prewarm`. Дёшево.

**2. Async save (фоновый поток для compress+fwrite)**:
- Игрок не видит ~100ms stall на каждом autosave.
- Реальная UX-фича, ~1 день работы.
- Патч в `alife_storage_manager::save` — выделить compress+w_open+w в `std::thread`, синхронизировать на shutdown.
- В backlog как `[P2] Async save thread`.

**3. SQLite metadata index для save list** (если сейвов станет >50):
- `.scop` остаётся на диске как есть.
- Параллельный `saves.db`: `(name, ts, level, location, journal, screenshot_path, hash)`.
- UI рендерит load menu из SQLite (мгновенно) вместо open+parse-header каждого `.scop`.
- Применять когда боль появится; у нас 9 сейвов — over-engineering.

### ❌ Не стоит делать

**RAM disk для `~/.openxray-data`** (overlay в памяти):
- Сэйвы теряются при крэше. TX-hang у нас регулярный → сэйвы важнее, чем 5ms.
- Сейчас и так быстро.

**tmpfs / `/private/var/tmp` для cache**:
- macOS не имеет canonical tmpfs. APFS на NVMe и так быстрый.
- Применять только если SSD деградирует.

**Redis для сэйвов**:
- Redis это in-memory K/V для high-frequency ops. `.scop` — 600KB binary, раз в 5 мин.
- AOF/RDB на диск делает тот же fsync — никакой экономии.
- Лишний демон, IPC hop, точка отказа.

**SQL DB для сэйвов (Postgres/MySQL)**:
- Накладные >> экономия.
- Полезно для multiplayer с persistent world (MMORPG-tier). У нас SP.

**mmap save files**:
- `mmap` полезен для random-access read больших файлов. У нас sequential write compressed blob.
- Замедлит первое чтение из-за page faults, не ускорит запись.

## Принцип

Прежде чем тащить технологию, померить узкое место. Большинство «ускорений» через RAM/БД здесь дадут <1% выигрыша при +20% сложности эксплуатации. Возвращаемся к этому файлу когда (а) появятся реальные I/O bottleneck'и (например, async level streaming в open-world моде), или (б) накопится сейвов 50+ и UI list начнёт тормозить.
