---
status: draft, audit-only
issue: A.2 (native-shell roadmap, ghttp worker offload pre-flight)
date: 2026-05-26
author: script-bridge (pre-flight audit)
---

# A.2 ctx-ownership audit (2026-05-26, pre-flight)

## Scope

Before moving ghttp polling to a serial `dispatch_queue` worker (with
main-thread fastdelegate dispatch via mutex+deque drain), every ctx pointer
passed into ghttp via `userData` must be classified. If any ctx is
Lua-bound or main-thread-only state that the worker would touch directly,
the plan needs adjustment.

Touched-by-worker surface in the planned design:

- worker thread calls `ghttpThink()`
- ghttp invokes `ProgressHandler` / `CompletedHandler` / `StringCompletedHandler`
  on the worker thread with the heap-allocated `*Context*` as `userData`
- those handlers either run the user's fastdelegate inline (current) or push
  `{delegate, args}` onto a drain queue (planned A.2)

So the worker only touches the `FileDownloadContext` / `StringFetchContext`
struct itself. The fastdelegate *target* (`CMainMenu*`) is only invoked once
the main thread drains the queue. **The audit question reduces to: does the
heap context itself hold any Lua-bound or main-thread-only state that the
worker dereferences inside the ghttp handler?**

## ghttp engine-side call sites

Only one wrapper layer (`CGameSpy_HTTP`) calls ghttp directly. Vendored
`src/utils/mp_gpprof_server/libraries/gamespy/ghttp/` is an offline server
tool, not the engine — explicitly out of scope.

| Site (file:line) | Caller | ghttp call | Ctx struct | Ctx members | Lua-bound? | Main-thread-only? | Classification | Action |
|---|---|---|---|---|---|---|---|---|
| `src/xrGameSpy/GameSpy_HTTP.cpp:110` | `CGameSpy_HTTP::DownloadFile` | `ghttpSaveExA` | `FileDownloadContext` (xr_new'd at :106, deleted at :84 in `CompletedHandler`) | `CompletionCallback`, `ProgressCallback` (both FastDelegate, by value) | no | no — handlers only read the two FastDelegate value-copies and (in the planned design) push them into the drain queue | **POD-heap** | safe; worker may delete after drain enqueue |
| `src/xrGameSpy/GameSpy_HTTP.cpp:123` | `CGameSpy_HTTP::FetchString` | `ghttpGetA` | `StringFetchContext` (xr_new'd at :121, deleted at :100 in `StringCompletedHandler`) | `StringCompletionCallback` (FastDelegate, by value) | no | no — same reasoning | **POD-heap** | safe; worker may delete after drain enqueue |
| `src/xrGameSpy/GameSpy_HTTP.cpp:135` | `CGameSpy_HTTP::StopDownload` | `ghttpCancelRequest` | n/a (no userData) | — | n/a | n/a | **N/A** (no ctx) | must be marshalled to worker thread; see notes below |
| `src/xrGameSpy/GameSpy_HTTP.cpp:11-13` | `CGameSpy_HTTP::StartUp/CleanUp/Think` | `ghttpStartup/Cleanup/Think` | n/a | — | n/a | n/a | **N/A** (no ctx) | `Think` is what moves to worker; `StartUp/CleanUp` stay on main, run once |

## CGameSpy_HTTP callers (fastdelegate targets)

These are the *delegate* targets, not ctx pointers — they are invoked only
on main after drain. Listed for completeness so the gate above can be
verified end-to-end.

| Site (file:line) | Caller | Delegate target | Touches main-only state? |
|---|---|---|---|
| `src/xrGame/MainMenu.cpp:841-842` | `CMainMenu::TriggerUpdateCheck` (main only) | `CMainMenu::OnManifestReceived` | yes — touches `m_sPDProgress`, `m_pMB_ErrDlgs`, `m_pendingManifest`. **Must run on main**, which the drain-queue design guarantees. |
| `src/xrGame/MainMenu.cpp:929-932` | `CMainMenu::OnPatchAcceptYes` (main only, UI callback) | `CMainMenu::OnUpdateDownloadCompleted` + `OnUpdateDownloadProgress` | yes — touch `m_sPDProgress`, `m_pMB_ErrDlgs`. **Must run on main.** |
| `src/xrGame/MainMenu.cpp:1006-1007` | `CMainMenu::CancelDownload` (main only) | n/a (calls `StopDownload` → `ghttpCancelRequest` directly) | this is the one site where main thread reaches into ghttp state; see UNSAFE note below |

## Findings

### Context payloads

Both `FileDownloadContext` and `StringFetchContext` are **POD-heap** by
the strict definition this audit uses:

- heap-allocated (`xr_new` in wrapper, `delete` in handler)
- contain only `FastDelegate` value copies (no raw lua_State*, no
  luabind::object, no CUIWindow*)
- no Lua references reach the worker — the FastDelegate is opaque to
  the worker until drained on main
- ownership is unambiguous: created by the call site, deleted by the
  one-shot completion handler (and on early-fail at :115/:128)

**No UNSAFE ctx sites found.**

### Not-quite-ctx hazard: `ghttpCancelRequest` and `m_LastRequest`

`CGameSpy_HTTP::StopDownload()` (line 132-137) reads `m_LastRequest`
(GHTTPRequest, an int) from main and calls `ghttpCancelRequest` directly.
After A.2 lands, ghttp's internal state machine lives on the worker
thread — `ghttpCancelRequest` from main is **a cross-thread mutation of
ghttp internals**, which the library does not document as thread-safe.

This is NOT a ctx-pointer issue (the question this audit was scoped to),
but it is a thread-safety hazard the A.2 plan needs to address. Options:

1. Marshal `StopDownload` onto the worker queue (post a "cancel
   m_LastRequest" task; main returns immediately).
2. Hold a mutex around all ghttp entry points (`Think`, `ghttpGet*`,
   `ghttpSave*`, `ghttpCancelRequest`) — simpler but adds contention.
3. Snapshot `m_LastRequest` atomically (`std::atomic<GHTTPRequest>`) and
   post cancel via worker task — same as (1) but spelled out.

Recommendation: **(1)**. Aligns with the "ghttp state lives on the worker"
model; `m_LastRequest` becomes worker-private after the dispatch_async
that starts the request returns.

### Not-quite-ctx hazard: early-fail paths invoke delegate inline on main

Lines 113-116 and 125-129 in `GameSpy_HTTP.cpp`:

```cpp
if (m_LastRequest < 0)
{
    completed(false);  // <-- main thread, before any worker handoff
    xr_delete(ctx);
}
```

This is fine today because everything is main-thread. After A.2, if the
ghttp call itself is dispatched onto the worker (e.g. `DownloadFile` queues
a task that calls `ghttpSaveExA`), the early-fail check moves to the
worker, and `completed(false)` would fire on the worker thread —
violating the "delegates run on main" invariant.

Mitigation: in the worker-side `DownloadFile` task, when `ghttpSaveExA`
returns `< 0`, enqueue the `completed(false)` into the drain deque instead
of calling it inline. Same path as the success case, just synchronous.

## Summary

- **Total ghttp call sites (engine)**: 5 in `src/xrGameSpy/GameSpy_HTTP.cpp`
  (Startup, Cleanup, Think, ghttpSaveExA, ghttpGetA, ghttpCancelRequest —
  6 if you count separately; lifecycle 3 + payload 2 + cancel 1).
- **Total ctx-bearing call sites**: 2 (`ghttpSaveExA`, `ghttpGetA`).
- **Distribution**:
  - POD-heap: 2 / 2 ctx sites
  - Lua-bound: 0
  - Main-only: 0
  - External: 0

## Verdict

**Yes — A.2 can proceed as planned.**

Both ctx payloads are POD-heap; no Lua state, no UI widget pointers, no
luabind objects cross the worker boundary. The fastdelegate targets (which
*do* touch main-only state) are invoked only via the drain queue on main,
exactly as designed.

**Two preconditions to fold into the A.2 plan before implementation:**

1. `StopDownload`'s `ghttpCancelRequest` call must be marshalled onto the
   worker queue, not called from main. `m_LastRequest` becomes
   worker-private.
2. Early-fail paths in `DownloadFile` / `FetchString` (when ghttp call
   returns `-1`) must enqueue `completed(false)` into the drain queue
   rather than calling it inline — so the "delegates run on main"
   invariant holds even for synchronous failures.

Both are mechanical and don't affect the gate decision.

## Open questions for Tech Lead

- Does the planned worker design include `StartUp` / `CleanUp`
  (`ghttpStartup` / `ghttpCleanup`) on the worker too, or only `Think`?
  If `CleanUp` runs from `~CGameSpy_HTTP` on main while there is still a
  pending `Think` on the worker, that's UAF inside ghttp. Either join
  the worker in `~CGameSpy_HTTP` or move `ghttpCleanup` onto the worker
  as its last task.
- `Externals/GameSpy/ghttp/` (the actual linked lib, not the
  `mp_gpprof_server` vendored copy) — does it use any TLS / static
  globals that assume single-threaded callers? Worth a 5-minute skim
  before implementation. Not blocking the gate.

### New landmine for the playbook

ghttp's `m_LastRequest` (and any analogous "last request id" field in
async-library wrappers) is a hidden cross-thread channel between the
main-thread API surface and the polling thread. When moving the polling
onto a worker, cancellation cannot stay on main — it has to ride the
same queue. Audit cancellation paths separately from completion paths
when offloading any async-poll library.

---

# Pre-flight B — GameSpy menu-init blocking-I/O audit (2026-05-26)

Sister audit: does main menu init touch blocking I/O even without MP browse?

## Finding

**Yes, one site identified.** `CGameSpy_Available::CheckAvailableServices`
in `src/xrGameSpy/GameSpy_Available.cpp:9-10` runs a synchronous spin-loop:

```cpp
GSIStartAvailableCheckA(GAMESPY_GAMENAME);
while ((result = GSIAvailableCheckThink()) == GSIACWaiting)
    msleep(5);
```

Called unconditionally from `CGameSpy_Full` constructor
(`GameSpy_Full.cpp:20`), which is instantiated unconditionally in
`CMainMenu` constructor (`MainMenu.cpp:130`) when `!isDedicatedServer`.

**Trigger**: every game launch into the main menu. SP-only player IS affected.

## Decision: scope out of A.2 (document as residual)

Rationale:

- A.2 gate is the **PR #76 nc-stall reproducer** specifically — Family 1.A
  ghttp polling chain. CheckAvailableServices is a one-time startup
  block, NOT per-frame, and NOT on the path the gate measures.
- In current behaviour the check returns quickly (game boots in normal
  time on launch) — GameSpy availability service likely returns a
  fast "unavailable" verdict; the structural risk is there but the
  practical exposure is low.
- Including this site expands A.2 scope significantly: the check
  signature returns bool by output param synchronously, so offloading
  changes the menu init contract (UI must show a "checking..." state).
  That is a separate UX change, not a hang fix.
- A.1 rescope principle: minimum to hit gate.

**Action**: file as A.2.1 follow-up issue (after A.2 merges). If a real
hang against this site is reproduced in user smoke, escalate; until
then, residual.

## Open questions parked

- `gsCoreInitialize()` at `GameSpy_Full.cpp:22` — synchronous; what does
  it do? (No urgent gate impact; covered under same A.2.1 follow-up.)
- The `5ms msleep` spin in CheckAvailableServices means main thread
  isn't fully blocked (yields to scheduler), but WindowServer events
  still don't get serviced between sleeps. On a real DNS stall this
  could still show as menu-load freeze. Worth measuring eventually.

