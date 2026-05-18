# Process supervision / «контейнер для игры»

Запрос (2026-05-18): «есть способ сделать так, чтобы процесс игры жил в каком-то окружении, которое система гарантированно может убить, чтобы у нас не оставались зомби. как в docker-контейнерах».

## Корень: zombie != userspace, а kernel-hold

То, что мы видим как «zombie» на max-settings — это `TX` state в `ps`: процесс держит kernel handle к GPU через IOKit и спит в неинтерраптерабельном `mach_msg`-ожидании. Это аналог Linux `D state` для GPU-driver-bound waits. **Ни один userspace-механизм не вытащит процесс из TX**, пока сам kernel не отпустит. Освобождают только:

- watchdog GPU-драйвера (30–90 сек, иногда срабатывает);
- logout сессии (`Shift+Cmd+Q`) — kernel при teardown агрессивнее;
- hard reset (hold power 10 сек).

Это значит, что **никакой «контейнер» не помог бы**, потому что:

- Linux namespace / cgroup / Docker — даже там процесс в `D state` не убивается извне. Docker контейнер «дохнет» когда дочерние процессы давно бы и так дохли — но `D state` он тоже не вытаскивает.
- macOS не имеет аналогов Linux namespace. `sandbox-exec` ограничивает права, но не даёт kill-control. `launchd` управляет жизненным циклом job-а, но `launchctl bootout` против TX-state процесса так же бессилен как `kill -9`.

## Что реально доступно, по уровням

| Подход | Что даёт | Лечит TX? | Стоимость |
|---|---|---|---|
| **setsid() + parent watchdog** | Чистый kill для userspace deadlock'ов (infinite loop в C++, ждущий мьютекс). Heartbeat-pipe или unix socket; родитель пингует, при отсутствии reply N сек шлёт `kill -9 -pgid`. | Нет | ~2 ч |
| **launchd-managed job** (`launchctl load ... ; launchctl bootout`) | Управление через системный сервис, чистый teardown по политике. | Нет | ~3 ч |
| **macOS Sandbox** (Seatbelt, `sandbox-exec`) | Изоляция прав (filesystem, network, IPC). Не про kill. | Нет | ~2 ч на политику |
| **macOS Virtualization.framework** (Apple Hypervisor) | Гостевая ОС в VM. Хост может прибить VM мгновенно вне зависимости от состояния guest-kernel. | **Да** | Недели; пока GL/Metal passthrough на arm64 macOS-guest незрелый — пришлось бы пересобрать движок под Linux guest |
| **Устранить операции, провоцирующие TX** | На roadmap уже: compressed 3D textures (water_sbumpvolume), MSAA framebuffer attachments, blocking occlusion (fixed), `glClientWaitSync` поведение. Это **единственный** «настоящий» путь к стабильному max-settings. | **Да** (косвенно: TX просто не наступает) | Дни-недели на каждую отдельную операцию; месяцы на полный Vulkan/Metal-backend |

## Рекомендация (план A)

1. **Сейчас**: написать тонкий supervisor — отдельный launcher-binary (`OpenXRay-supervisor`), который форкает `xr_3da` через `setsid()`, держит pipe для heartbeat, и убивает всю process group при тишине ≥30 сек или при получении сигнала от пользователя. Не лечит TX, но:
   - убирает userspace zombie (deadlock, infinite loop в Lua/script handler);
   - даёт явный лог о том, что процесс вышел и почему;
   - даёт хук «process exited; cleanup overlay symlink / log rotation / диалог пользователю».
2. **В параллель**: продолжать roadmap по устранению TX-провокаторов (Vulkan/Metal renderer как стратегический финал).
3. **Не делать сейчас**: launchd-конфиг (overhead для решения, которое всё равно не лечит главную проблему), Sandbox (не наша проблема, у нас не security-issue), VM (несоразмерно).

## Если когда-нибудь будет надо «гарантированно убить»

Единственный действительно работающий путь — VM. Это уходит в стратегический пересмотр: «делаем ли мы macOS-нативный движок, или мы упаковываем Linux-сборку в Hypervisor.framework как Steam Play / Crossover делают». Этот разговор имеет смысл только если мы найдём, что Metal/Vulkan backend стоит дороже, чем VM-обёртка над уже работающей Linux-сборкой. Сейчас Metal/Vulkan дешевле и даёт лучший UX (без оверхеда).
