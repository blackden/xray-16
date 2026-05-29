# A.7.4 — native render foundation (3-PR series, 2026-05-29)

Замена SDL2 owning'а window + GL context на собственный native на macOS-only форке. После parked PR #185 (single-shot rewrite сломался по three independent symptoms) подход переписан как step-by-step instrumentation-first серия с per-step env var gating. Три PR merged подряд в один день.

## Decomposition: три PR вместо одного

**Decision.** Разбить A.7.4 на 3 PR (foundation → ctx → window), каждый под отдельным env var, default OFF — никакой регрессии при merge:

- **PR #187 (gitea #186)** A.7.4-restart foundation — instrumentation infrastructure (probe NSWindow + NSOpenGLContext caps, `==> a74p[...]` DLOG'и в `CHW::Present` + `CInput::GrabInput`) + опциональный native swap (`OPENXRAY_NATIVE_SWAP=1` → SDL'овский ctx → `[ctx flushBuffer]`).
- **PR #189 (gitea #188)** native NSOpenGLContext — собственный ctx (`OpenXRay_NativeGL_CreatePersistent`) attached к SDL'овскому contentView под `OPENXRAY_NATIVE_GL=1`. SDL_GL_MakeCurrent bypass (SDL'овский tracking отвергает наш ctx).
- **PR #191 (gitea #190)** native NSWindow — собственное окно visible alongside SDL'овского под `OPENXRAY_NATIVE_WINDOW=1`. Render через **наш** contentView. Implicit native swap (потому что наш ctx attached к нашему view).

**Why.** PR #185 single-shot заменял всё разом (window + ctx + swap) и failed по three symptoms которые невозможно было raznestit:
- Renderer probe FATAL (test_hw helper не ожидал что CHW::CreateDevice игнорирует SDL_Window arg)
- Resize hang (drain в Present вызывал `Reset()` per frame во время Cocoa fullscreen анимации)
- Render not presenting (engine pFB → FBO 0 blit фейлил на native NSOpenGLContext, который не считался complete до first `[ctx update]`)

Smoke session не мог diagnose root cause каждого по отдельности — все три проявлялись как «чёрный экран и hang». Team-lead консилиум: park PR #185, разбить на mini-steps под env var → каждый step можно validate (визуально + log) изолированно.

**Trade-off.** Три PR ceremony вместо одного. m_sdlWnd остаётся в коде на native path — engine'овская infra (`Device_imgui.cpp`, `Device_mode.cpp`, `xr_input.cpp`, `Device.cpp` focus probe) всё ещё refer'ит к нему. SDL'овское окно остаётся visible parallel под `OPENXRAY_NATIVE_WINDOW=1` — известная ограниченность, решаем в отдельном PR (C.4 в #190 comments).

**Revisit if.** Пайплайн расширяется на Vulkan/Metal — это будет уже четвёртая итерация, тогда переосмысливаем декомпозицию.

## SDL_GL_MakeCurrent rejects external NSOpenGLContext

**Decision.** Bypass SDL_GL_MakeCurrent + SDL_GL_GetCurrentContext в `CHW::MakeContextCurrent` + `CHW::GetCurrentContext` под `s_nativeGLOwned=true`. Вызываем `[ctx makeCurrentContext]` напрямую через `OpenXRay_NativeGL_MakeCurrentArg`.

**Why.** SDL2 на macOS поддерживает internal `SDL_VideoData::tracking` для contexts created via `SDL_GL_CreateContext`. Если в `SDL_GL_MakeCurrent(window, ourCtx)` передать context не из SDL'овского pool'а, оно возвращает ошибку «Invalid OpenGL context» (мы это поймали в логах PR #189 — `main returning with code 1` сразу после Inspect_Context). `SDL_GL_GetCurrentContext` возвращает `nullptr` для нашего ctx (tracking не находит его в pool'е).

На macOS SDL2 Cocoa backend под капотом всё-равно делает `[NSOpenGLContext makeCurrentContext]` — наш direct call'у эквивалентен. Bypass — единственный clean путь использовать собственный context.

**Trade-off.** `s_nativeGLOwned` flag в `glHW.cpp` ветвит lifecycle (Create / Destroy / MakeCurrent / GetCurrent / Swap). Если в будущем engine добавит ещё SDL_GL_* calls — придётся ветвить и их.

**Revisit if.** Уходим на Vulkan/Metal — SDL2 OpenGL APIs больше не релевантны.

## NATIVE_WINDOW=1 implies NATIVE_GL + NATIVE_SWAP

**Decision.** Один env var `OPENXRAY_NATIVE_WINDOW=1` включает полный native render path. `useOurCtx = useNativeGL || useNativeWindow` для context create. `nativeSwap = useNativeSwap || s_nativeWindowRender` для swap.

**Why.** Логически NATIVE_WINDOW не имеет смысла без native ctx — рендер пошёл бы через SDL'овский ctx attached к SDL'овскому view, а наше окно осталось бы пустым. Аналогично NATIVE_SWAP — без него SDL_GL_SwapWindow swap'нул бы SDL'овский context's FBO 0, не наш → опять чёрное наше окно. Force'им сразу полный path под одним флагом — нельзя случайно собрать инвалидную комбинацию.

**Trade-off.** Matrix test'ы для отдельных combinations теряют smysl (NATIVE_GL alone валиден для validation что наш ctx работает на SDL'овском view — это step B.2). Но мы оставили это для diagnostic — `OPENXRAY_NATIVE_GL=1` alone всё ещё работает (attaches к SDL'овскому contentView).

**Revisit if.** Появится сценарий «отдельный native context на отдельном SDL window» — например для secondary viewport. Тогда придётся отделить flag'и.

## SDL_HideWindow не работает для скрытия SDL_Window

**Decision.** Принять что SDL'овское окно остаётся visible parallel под `OPENXRAY_NATIVE_WINDOW=1`. Документировать как known limitation. C.4 = skip SDL_CreateWindow на Apple вообще.

**Why.** В A.7.4c (PR #191) пытались `SDL_HideWindow(m_sdlWnd)` сразу после Show нашего окна, затем `SDL_HideWindow` ещё раз defensively в `CHW::CreateDevice` — оба не работали. `SDL_SetWindowPosition(-10000, -10000)` чтобы убрать за экран — это костыль (ragnar явно его отверг: «никаких пряток»). Корневая причина: engine'овский `UpdateWindowProps` / vid_mode setup / `SDL_SetWindowFullscreen` пересоздают окно visible state после нашего hide. Hide ловит окно только в момент вызова — следующий vid_mode tick его опять показывает.

Единственный clean path — `SDL_CreateWindow` skip вообще под `OPENXRAY_NATIVE_WINDOW=1`. m_sdlWnd = nullptr. Затем все consumers Apple-condition'ить (ImGui viewport, vid_mode picker, cursor `SDL_SetWindowGrab`, focus probe, `SDL_SetWindowHitTest`, `SDL_GetWindowDisplayMode`). Это big refactor — отдельный PR в C.4.

**Trade-off.** До C.4: с `NATIVE_WINDOW=1` пользователь видит два окна (наше с меню + SDL'овское чёрное pre-render placeholder). Focus идёт в SDL'овское — нельзя clicать наше окно, Cmd+Ctrl+F не работает. Но render работает в нашем окне — это уже доказывает viability native path.

**Revisit if.** Найдётся неинвазивный SDL2 API чтобы навсегда отключить engine'овский `UpdateWindowProps` show — но maintenance того не стоит (mы всё равно дойдём до full strip в A.7.5).

## C-series process drift — manual edits вместо specialist dispatch

**Decision.** A.7.4c (C.1-C.3 mini-steps) делал foreground manual edits, не dispatch'ил `apple-platform` для force-activate / SDL_HideWindow attempts. **Это была ошибка** — два hack'а подряд (force-activate в C.1, SDL_HideWindow + offscreen в C.3) caught'ы пользователем как «костыль» и reverted.

**Why это произошло.** Каждый step выглядел «мал» (DLOG здесь, env var там, 20 строк edit). Specialist dispatch overhead казался слишком тяжёлым для tiny commits. Iteration tempo был быстрый (log → fix → log → fix). После 6 часов session с successful manual edits набрал inertia.

**Why это было ошибочно.** Apple-platform agent на premise audit сразу сказал бы:
- C.1: «force-activate не работает с SDL_PumpEvents — Cocoa runloop reset'ит focus каждый frame»
- C.3: «SDL_HideWindow не работает — UpdateWindowProps undoes; нужен skip SDL_CreateWindow вообще»

Лекарство: per `feedback_delegate_to_specialists` foreground = координатор, не implementer. C.4 явно specialist-level (big touchpoint refactor через apple-platform + cpp-engineer review).

**Trade-off.** Specialist dispatch добавляет ceremony на tiny commits. Но cost двух reverted hack'ов выше — это два workshop hours + два commits + два revert commits.

**Revisit if.** Никогда — это process learning который мы расширяем в `feedback_delegate_to_specialists` memory. C.4 пойдёт правильно: apple-platform premise audit → enumerate consumers → implementation pass.

## C.4a — skip SDL_CreateWindow on Apple под NATIVE_WINDOW=1 (PR #193, master `f82a51abc`)

Structural fix вместо C.3 SDL_HideWindow hack'а. Под `OPENXRAY_NATIVE_WINDOW=1` `SDL_CreateWindow` skip'ается полностью; `m_sdlWnd = nullptr`; всё переводится на наш NSWindow + NSOpenGLContext + NSWindowDelegate. Default mode не задет — всё за gate `Device.m_useNativeWindow`.

### GLAD proc-loader hotfix — dlsym vs SDL_GL_GetProcAddress (landmine)

**Decision.** На Apple+native (s_nativeGLOwned) GLAD загружается через `dlsym(RTLD_DEFAULT, name)` против OpenGL.framework. На non-native path остаётся `SDL_GL_GetProcAddress`.

**Why.** Audit C.4a пропустил **implicit dependency**: gladLoadGL получает proc-loader, и engine на boot вызывал `gladLoadGL(SDL_GL_GetProcAddress)`. Без `SDL_CreateWindow` нет `SDL_GL_CreateContext` → SDL не загружало драйвер → `SDL_GL_GetProcAddress` возвращает «No GL driver has been loaded» → GLAD получает nullptr loader → все gl* указатели null → segfault на первом draw. Smoke поймал (exit=139). OpenGL.framework на macOS статически линкуется в процесс, gl* символы уже в loader'е — `dlsym(RTLD_DEFAULT, name)` находит их напрямую без SDL intermediate.

**Trade-off.** Дополнительная ветка в `glHW.cpp:gladLoadGL` под `#if XR_PLATFORM_APPLE && s_nativeGLOwned`. Минимальная — два include'а (`<dlfcn.h>`) + static функция-обёртка + if-branch.

**Revisit if.** Уходим на GLAD2 / другую loader-схему — или на Vulkan/Metal, где gl* irrelevant.

**Landmine class.** Pre-implementation audit должен спрашивать «что **неявно** зависит от состояния которое мы убираем?» — не только direct call sites через grep. GLAD ↔ SDL ctx implicit link не вылез через grep на `SDL_*Window*`. Урок зафиксирован в feedback memory.

**History.** Hotfix commit `4fbe371da` был применён локально во время C.4a smoke (29 мая) и работал. **Не попал в PR #193 при merge** — commit остался только в локальной branch'е, не cherry-pick'нулся в integration tip перед PR merge. Codify earlier ошибочно записал что fix folded в `a9dd1fdb1`. Регрессия вскрылась когда ragnar запустил NATIVE_WINDOW=1 на свежем master (30 мая) — иконка прыгала в dock, GLAD init fail. Восстановлено через cherry-pick на новую ветку → PR #199 → merge master `a74b620d8`. См. memory `feedback_verify_hotfix_landed_in_pr`.

### NSWindowDelegate enqueue-only + engine-tick drain (ownership-style mitigation)

**Decision.** `OXRayNativeWindowDelegate` (`native_window.mm`) — Obj-C класс с методами `windowDidBecomeKey/ResignKey`, `windowDidResize/Move`, `windowDidMiniaturize/Deminiaturize`, `windowWillClose:`. Каждый метод **только enqueue'ит** в single-slot last-wins aggregator. Никаких прямых вызовов в engine. Drain через existing `OpenXRay_RunPerFrameMacOSHooks` engine-tick. C-trampolines (`OpenXRay_NativeWindow_PollEvents`) подцепляются в `Engine.cpp` к engine consumers (Reset, OnWindowActivate, RequestGracefulShutdown).

**Why.** Cocoa fundamentally owns window event dispatch — driver-style API для resize/close/focus отсутствует. Если бы delegate method'ы вызывали engine state directly (как A.7.2 attempt с `NSTextInputContext.activate` + ручной `handleEvent:`), AppKit dispatch ran бы параллельно с engine tick → double-fire / race / unpredictable order. Pattern enqueue-only + explicit-tick drain — это тот же подход что отработал в `g_pendingLifecycleEvent` (A.1 NSApplicationDelegate).

**Trade-off.** Лишний indirection слой: delegate → queue → trampoline → engine. ~30 LOC C-ABI + ~50 LOC Obj-C. Один frame latency на propagation event'а в engine (acceptable — Cocoa events идут с 60+ Hz, engine tick на той же частоте).

**Revisit if.** Нужны events с sub-frame latency (например, key down/up в input pipeline — но те уже идут через `NSEvent` local monitor, не window delegate).

### OnWindowActivate signature refactor (sentinel-collision fix)

**Decision.** Сигнатура `OnWindowActivate(SDL_Window* window, bool active)` → `OnWindowActivate(bool isMainWindow, bool active)`. Все callers обновлены: `device.cpp` event-loop, `x_ray.cpp:457,469,489`, `Engine.cpp:244-250`, новый trampoline для delegate `on_activate`.

**Why.** Старая сигнатура использовала `window == m_sdlWnd` как «это main window» check. Когда `m_sdlWnd == nullptr` на Apple+native И event приходит с `window == nullptr` (например, secondary viewport WMEvent demux) — equality `nullptr == nullptr` → branch flips на «main», edit-mode activation мисроутится. Subtle bug, audit §4.5 поймал. Refactor устраняет sentinel collision полностью.

**Trade-off.** Touches ~5 файлов signature-wise; commit отдельно для clean bisect (behavior-neutral на default mode).

**Revisit if.** Никогда — это чистый improvement, замены не нужно.

### Single source of truth via `Device.m_useNativeWindow` field

**Decision.** Env var `OPENXRAY_NATIVE_WINDOW` читается **один раз** в `Device_Initialize.cpp` при boot, сохраняется как `bool Device.m_useNativeWindow`. Все consumers (12 файлов на Apple-conditioned путях) читают через C-ABI getter `OpenXRay_IsNativeWindowRender()`. **Никаких `getenv` re-reads в hot path** или branch checks.

**Why.** Craft-check принцип 1 (single source of truth). Repeated `getenv("OPENXRAY_NATIVE_WINDOW") != nullptr` на 7+ call sites — рискованно: один сайт может опустить gate, или env var может «измениться» в runtime (юзер вряд ли, но дальше rabbit hole). Field + getter — единая правда.

**Trade-off.** Лишний CHW header include / forward decl где `OpenXRay_IsNativeWindowRender()` нужен. Linker дешёв.

**Revisit if.** Уходим на runtime cvar (e.g. `r_native_window` через console) — тогда reads должны идти через cvar субсистему, не env var snapshot. Но это уже после A.7.5 SDL strip.

## #196 backspace в save-name — Option C deferred to view-attach phase (2026-05-30)

**Decision.** Backspace bug в save-name input field (gitea #196) **не фиксим quick-fix'ом**. Откладываем до A.7.4 view-attach фазы когда `OpenXRayTextInputView` будет добавлен в NSWindow's content view / responder chain как proper text-editing client.

**Why.** Investigation на ветке `issue-196-backspace-savename-diag` (BS-TRACE probes + 2 failed foreground fix attempts + apple-platform read-only specialist audit) выявил root cause: detached client view (`NSZeroRect`, не attached к window content view) заставляет AppKit fallback'ить control codepoints (0x08 backspace, 0x1b escape, etc.) напрямую в `insertText:` вместо routing через `doCommandBySelector:`. Foreground attempts с no-op NSResponder method stubs (commit `9590c954d`) и затем dispatch через `performSelector:` в overridden `doCommandBySelector:` (commit `f2b6add81`) **не сработали** потому что AppKit до `doCommandBySelector:` на detached view не доходит — fallback path таверной у Apple's interpretKeyEvents:.

Два варианта fix'а после specialist verdict:
- **Option A (quick):** Filter control bytes (`< 0x20 || == 0x7F`) в `OpenXRayTextInputView::insertText:` где первый раз эти байты попадают в engine code. 2 строки. Маскировка — AppKit продолжает слать мусор, мы игнорируем. Не лечит причину.
- **Option C (structural):** Attach view к NSWindow's content view / responder chain. AppKit увидит proper text-editing client → route'ит commands через `doCommandBySelector:` (наши NSResponder stubs из failed attempts тогда станут используемыми, либо вообще не нужны если просто fall through к A.3 ring). Лечит причину, требует window plumbing.

Workaround в ожидании fix'а: `save <filename>` через консоль (` ` toggle). Использует другую input pipeline (XR_IOConsole → Device.editor() ImGui InputText, без detached NSTextInputContext'а). Работает.

**Trade-off.** Quick-fix Option A был доступен прямо сейчас (10 минут работы), но это маскировка не structural. Принцип «correctness over throughput» (см. memory `feedback_correctness_over_throughput`) + «не оставлять костыли» — выбрали отложить до правильного решения. Цена: backspace в save-name dialog не работает пока A.7.4 view-attach не сделан; единственный affected workflow — F2 save game через menu, alternative path (console) работает. Acceptable для personal fork пока единственный тестер (ragnar) использует console route.

**Revisit if.** A.7.4 view-attach задержится >2 недель — пересмотреть применение Option A как interim до A.7.4. Или если появится другой affected surface (MP chat, network connect dialog, etc.) — расширит impact, повысит priority.

**State.**
- Issue #196 на gitea — open, labels `deferred` + `blocked-by-a7-4`, со специфическим root cause описанием в comment.
- Branch `issue-196-backspace-savename-diag` на gitea (НЕ merged) содержит investigation history:
  - `ea742e1ed` BS-TRACE [6/7][7/7] probes pre/post в `line_edit_control::on_key_press`
  - `b9f7f6c6f` BS-TRACE [TI][SE] probes в `on_text_input` + `set_edit`
  - `9590c954d` failed attempt — NSResponder no-op stubs
  - `f2b6add81` failed attempt — doCommandBySelector dispatch через performSelector
- TaskList #13 трекает «A.7.4: attach NSTextInputContext view to NSWindow responder chain (closes #196)».

См. `notes/reference/engine-map.md` Apple gotchas — «NSTextInputContext detached client view → control byte fallback» + memory `project_nstextinput_detached_fallback`.

## Links

- PR #187 (gitea #186) — foundation, master `5d1109f5e`
- PR #189 (gitea #188) — native NSOpenGLContext, master `723eca5f6`
- PR #191 (gitea #190) — native NSWindow visible coexistence, master `685f35bea`
- PR #193 (gitea #192) — C.4a skip SDL_CreateWindow, master `f82a51abc`
- Parked PR #185 (gitea #166) — single-shot rewrite, original A.7.4 attempt
- Follow-ups: gitea #195 (C.4b — UpdateWindowProps + ImGui + gamma), gitea #194 (pre-existing water_sbumpvolume.dds 0x500)
- `notes/reference/engine-map.md` Apple gotchas — see SDL Cocoa internals + pFB pipeline + GLAD-dlsym bullets
- `feedback_delegate_to_specialists` memory — updated с C-series cautionary tale
- `feedback_premise_audit_roadmap_steps` memory — updated с implicit-dependency check
