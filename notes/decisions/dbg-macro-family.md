# DBG_* macro family — design decisions

Architectural choices made for the gated debug-trace macro family
shipped in PR #202 (closes gitea #201). Where the design has
non-obvious alternatives, this doc records why we picked what we did
so future-me doesn't redo the wrong-turn analysis from scratch.

See also:
- Header: `src/Common/DbgTrace.hpp`
- Conventions / lifecycle / anti-patterns: `notes/conventions/debug-tracing.md`
- engine-map pointer: `notes/reference/engine-map.md` → Debug toolkit
- Cvar gotchas surfaced during build: `notes/reference/engine-map.md`
  → Cvars (engine console)

---

## Three macros, not four (2026-05-30, PR #202)

**Decision.** Ship `DBG_TRACE`, `DBG_TRACE_POST`, `DBG_ONCE`. Do NOT
ship `DBG_SCOPE` (RAII enter/exit pair).

**Why.** On the input-dispatch tree (NSEvent → A.3 ring → IR_OnKeyboardPress
→ CMainMenu → CDialogHolder → CUIDialogWnd → CUIWindow → CUICustomEdit
→ line_edit_control) a scope macro produces 5-7 events per call. At
~10 Hz typing that's ~70 events/sec for ONE key. Drowns the channel
before the actual signal lands. Also MasterGold has exceptions OFF
(`XRAY_EXCEPTIONS=0`); mid-scope throw with RAII destructor running
the trace would be UB-adjacent.

**Trade-off.** No automatic enter/exit pairing. If you want to know
«did this function return», you write two explicit `DBG_TRACE` calls
yourself. That's verbose but explicit.

**Revisit if.** A real recurring need shows up where the cost of
typing two traces beats the cost of log noise (e.g., async callback
chains where order is non-obvious). Then add `DBG_SCOPE` with a
documented frame-rate caveat.

---

## Bitmask cvar, not per-category cvars (2026-05-30, PR #202)

**Decision.** One cvar `dbg_mask` (int, bitmask) instead of seven
separate `dbg_input`/`dbg_render`/... cvars.

**Why.** Eight cvars pollute `cmd_list` autocomplete + config dumps.
Single int = one branch on the gate (`if (g_dbg_mask & (cat))`),
branch-predictor-friendly because zero in steady state. UX via helper
commands (`dbg_on input`/`dbg_off input`/`dbg_status`) translates
between human-readable category names and bits — best of both worlds.

**Trade-off.** Bit positions are opaque memory-load (need `dbg_status`
or the convention doc to recall `0x20 = audio`). Mitigated by the
helper commands being the primary UX path.

**Revisit if.** Categories grow past ~12 (then bits start running out
of `int`-range mental capacity, and we'd want `Flags64` or a token
list).

---

## Sub-categorization via format-string prefix (2026-05-30, PR #202)

**Decision.** Distinguish `[TI]` (text-input) vs `[SE]` (set_edit) vs
`[6/6] PRE` etc. via the format string passed to `DBG_TRACE`, not via
adding `DBG_CAT_INPUT_TEXT` / `DBG_CAT_INPUT_DISPATCH` enum bits.

**Why.** Categories are coarse runtime gates; sub-tags are fine-grained
log filters. Conflating them explodes the enum and means every new
investigation needs a code change to introduce a new sub-bit. Format
prefix is zero-cost (free with the format string anyway) and lets ad-hoc
grep work: `grep "\[DBG\].*\[TI\]"`.

**Trade-off.** No compile-time check that sub-tags are spelled
consistently. Convention doc lists the established prefixes.

**Revisit if.** Sub-tag count per category exceeds ~5-6 and grep
ergonomics degrade.

---

## MasterGold strip, not per-category compile-time toggles (2026-05-30, PR #202)

**Decision.** A single `#if !defined(MASTER_GOLD)` wraps the entire
macro family. No per-category `#define DBG_ENABLE_INPUT` etc.

**Why.** This is a personal fork. The user isn't going to selectively
compile-out one category — the only relevant axis is «ship vs dev».
Per-category compile gates would multiply build configs without payoff.

**Trade-off.** If we someday want to ship a build with INPUT
diagnostics on (for a customer-visible bug repro) we'd need a
build-flavor variant. Not on the roadmap.

**Revisit if.** Releasing a debug-instrumented build to non-developers
becomes a use case.

---

## g_dbg_mask in xrCore, not xrEngine (2026-05-30, PR #202)

**Decision.** Define `g_dbg_mask` in `xrCore/log.cpp` with `XRCORE_API`
linkage. Register cvar + helper commands in `xrEngine/xr_ioc_cmd.cpp`
mutating the global by reference.

**Why.** Initial draft placed `g_dbg_mask` next to `g_dev_tools` in
`xr_ioc_cmd.cpp` with `ENGINE_API` linkage. LSP immediately flagged it:
`Common/DbgTrace.hpp` declares `extern ENGINE_API int g_dbg_mask;`, and
`ENGINE_API` lives in `xrEngine/Engine.h` — a header that Common/-tier
files shouldn't pull. The header wasn't truly self-contained; build
passed only because PCH masked it. Moving the storage to xrCore (which
`xrCore/log.h` is in — already included by DbgTrace.hpp for `Msg`) means
the header now declares `extern XRCORE_API int g_dbg_mask;` with the
linkage macro already in the include chain. Self-contained, no layering
violation beyond what `Msg` already imposes.

**Trade-off.** Variable storage lives away from its primary use site
(cvar registration). Slightly surprising for someone looking for «where
is g_dbg_mask defined» — they'd start in xr_ioc_cmd.cpp. Mitigated by
the engine-map cvar-gotchas section pointing here.

**Revisit if.** A reorganization moves `Msg` itself out of xrCore (then
the whole include chain rebalances).

---

## Accepted layering ⚠: Common/ pulls xrCore (2026-05-30, PR #202)

**Decision.** `src/Common/DbgTrace.hpp` includes `xrCore/log.h` for
`Msg`. This violates the principle that `src/Common/` is a zero-deps
floor (see `PostLogMark.hpp` for the clean counter-example).

**Why.** `DBG_TRACE` must dispatch through `Msg` to integrate with the
engine log (timestamps, level filter, log-tail tooling). Three options
considered:

1. Pull `xrCore/log.h` directly — chosen. Practical violation: all
   current consumers (xrGame, xrUICore, xrEngine, .mm shims) already
   pull xrCore via PCH, so the formal layering breach has zero
   real-world impact.
2. Function-pointer dispatch wired at engine init (e.g., `xrCore`
   registers `Msg` into a slot DbgTrace uses) — rejected as
   over-engineering for personal fork. Adds a one-time init hazard
   (if a static destructor fires DBG_TRACE pre-init, segfault).
3. Two separate macros (`DBG_TRACE_LOG` via `Msg`, `DBG_TRACE_BARE`
   via stderr) — rejected, multiplies surface area for unclear gain.

**Trade-off.** Future module added to `src/Common/` whose author wants
to use DBG_TRACE will discover the dependency chain by following the
include. `PostLogMark.hpp` remains the clean alternative for that case.

**Revisit if.** Someone wants `DBG_TRACE` from a static destructor or
a context where xrCore isn't initialized yet. Then `DBG_TRACE_POST`
(POSTLOG_MARK route, zero xrCore deps) is the answer — switch to it.

---

## DBG_ONCE thread-safety via std::atomic CAS (2026-05-30, PR #202)

**Decision.** `DBG_ONCE` uses `std::atomic<bool>` with
`compare_exchange_strong` for the «fired» flag, not bare `static bool`.

**Why.** Engine has multiple producer threads — ghttp worker, sound
mixer, main thread. Any of them could hit the same `DBG_ONCE` call
site (e.g., a `DBG_ONCE(DBG_CAT_NET, ...)` in code shared by ghttp
+ main). C++11 magic statics guarantee init thread-safety, NOT
mutation. Bare `if (!fired) { fired = true; Msg(...); }` would
double-print under contention.

**Trade-off.** One atomic CAS per call site lifetime (after that, the
fast-path is a load-acquire). Negligible.

**Revisit if.** Profiling shows the atomic dominates a hot path
(extremely unlikely — DBG_ONCE fires literally once per process).

---

## Default `dbg_mask = 0` in all configs, no persistence (2026-05-30, PR #202)

**Decision.** `dbg_mask` defaults 0 in Debug/Mixed/Release/MasterGold.
`CCC_DbgMask::GetStatus` deliberately not overridden — base class
returns empty string → `IConsole_Command::Save` skips → cvar does NOT
get written to user.ltx → next launch starts with 0 again.

**Why.** DBG_TRACE output is high-volume diagnostic; opt-in per
session is the right invariant. Default-on in dev would drown the log
in the 80% of sessions where we're NOT actively chasing a specific
category. Persistence across launches breaks the per-session opt-in:
ragnar's first smoke set `dbg_mask 0x7F`, Cmd+Q, relaunched, mask was
0x7f — surprising and wrong per the convention.

The Save path was discovered during smoke: my first draft of
`CCC_DbgMask::GetStatus` returned `xr_sprintf(S, "0x%02x", g_dbg_mask)`
so the cvar would print its value to config dumps. Side effect:
`IConsole_Command::Save` (base, not overridden) uses GetStatus to
decide what to persist — non-empty string = save it. Fixed by removing
the GetStatus override; inspection happens through `dbg_status` (a
separate command, not the cvar).

**Trade-off.** If you want to keep a mask across launches you have to
flip it manually on each session start. Acceptable for the opt-in
discipline; if it becomes painful, add an autoexec.cfg-style override
ragnar can edit.

**Revisit if.** A use case shows up where the same investigation runs
across many launches and the manual re-flip is friction. Then either
allow persistence (drop the principled stance) or build a per-session
auto-enable cfg.

---

## Pilot parked in code, not removed (2026-05-30, PR #202)

**Decision.** 12 BS-TRACE probes from `issue-196-backspace-savename-diag`
landed converted in master as `DBG_TRACE(DBG_CAT_INPUT, ...)`. Each
carries `// XXX [foreground] DBG-PARKED-196: backspace pipeline trace`
on the line above, signaling deliberate parking.

**Why.** Probes will be needed in Task #13 (A.7.4 Option C view-attach
fix for #196). Stripping them now means re-discovering insertion points
+ re-writing format strings later. Parking with the explicit XXX tag
matches the lifecycle policy in the convention doc — strip-on-close is
default, parking is the documented exception. Cost-of-park is zero in
MasterGold (stripped at compile time) and 7-bit-gate in dev (zero in
default-0 state).

**Trade-off.** Future-me reading line_edit_control.cpp sees DBG_TRACE
calls that look orphaned without the issue context. The XXX
`DBG-PARKED-196:` comment is the bridge — grep for that exact prefix
finds all parked probes per issue.

**Revisit if.** Task #13 lands and either uses the probes (then strip
post-merge) or doesn't (then re-evaluate: still parking, or remove?).
