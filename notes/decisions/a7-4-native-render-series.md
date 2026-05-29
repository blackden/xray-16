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

## Links

- PR #187 (gitea #186) — foundation, master `5d1109f5e`
- PR #189 (gitea #188) — native NSOpenGLContext, master `723eca5f6`
- PR #191 (gitea #190) — native NSWindow visible coexistence, master `685f35bea`
- Parked PR #185 (gitea #166) — single-shot rewrite, original A.7.4 attempt
- `notes/reference/engine-map.md` Apple gotchas — see SDL Cocoa internals + pFB pipeline bullets
- `feedback_delegate_to_specialists` memory — updated с C-series cautionary tale
