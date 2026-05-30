# Debug-tracing convention — `DBG_TRACE` macro family

Single source of truth for non-shipping debug traces in the engine.
Lives at `src/Common/DbgTrace.hpp`. Bound to cvar `dbg_mask` registered
in `src/xrEngine/xr_ioc_cmd.cpp`.

## Macros

| Macro | Route | Use when |
|---|---|---|
| `DBG_TRACE(cat, fmt, ...)` | `Msg(...)` → engine log | normal investigation, message goes through level filter + remote-debug |
| `DBG_TRACE_POST(cat, fmt, ...)` | `POSTLOG_MARK_FMT(...)` → stderr | debug-only shutdown investigation, after `Core._destroy` closes the engine log |
| `DBG_ONCE(cat, fmt, ...)` | `Msg(...)`, one-shot per process | per-frame paths where you want one warning, not a flood; thread-safe via `std::atomic` |

House convention (mirrors `PostLogMark.hpp`): variadic macros need at
least one format argument. No GNU `##__VA_ARGS__`. Pass a literal `""`
if the message has no substitutions.

## Categories

Fixed bit enum in `DbgTrace.hpp` (`enum DbgCategory`):

| Bit | Name | Domain |
|---|---|---|
| `0x01` | `DBG_CAT_INPUT` | keyboard/mouse/text-input pipeline, SDL drain, key dispatch |
| `0x02` | `DBG_CAT_RENDER` | render init, frame, swapchain, GL state |
| `0x04` | `DBG_CAT_LIFECYCLE` | boot/shutdown, level load/unload, destructor chains |
| `0x08` | `DBG_CAT_FS` | LocatorAPI, path resolution, archive open |
| `0x10` | `DBG_CAT_ALIFE` | ALife server, online/offline switching, scheduler |
| `0x20` | `DBG_CAT_AUDIO` | OpenAL backend, streaming, EFX |
| `0x40` | `DBG_CAT_NET` | netserver, demo playback, multiplayer state |
| `0x7F` | `DBG_CAT_ALL` | shortcut for «enable everything» |

Sub-categorization (e.g. `[1/6]`, `[TI]`, `[SE]`) is expressed via the
format-string prefix, NOT by adding more enum bits. Each new feature
adding seven bits explodes the design — keep categories coarse.

## Gates

- **Compile-time** — under `MASTER_GOLD` all macros expand to
  `((void)0)`. Zero runtime cost, no leaked format strings in the
  shipped binary.
- **Runtime** — `g_dbg_mask` (cvar `dbg_mask`) is an int bitmask.
  A probe fires only when its category bit is set. Default mask is
  `0` in EVERY config — debug traces are opt-in per session,
  regardless of build flavour.

Console commands:

```
dbg_mask 0x01            # raw bitmask set
dbg_on input             # symbolic set
dbg_off input            # symbolic clear
dbg_status               # print mask + per-category on/off
```

Symbolic names: `input`, `render`, `lifecycle`, `fs`, `alife`, `audio`,
`net`, `all`.

## Lifecycle policy

Default behaviour at issue closure = **strip the probe**. A trace
landed during investigation is debt unless explicitly justified.

Parking a probe past issue closure is the exception. To park:

```cpp
// XXX [author] DBG-PARKED-<issueN>: rationale
DBG_TRACE(DBG_CAT_INPUT, "...");
```

The `XXX` line above the call is mandatory. It signals to future-self
(and grep-driven cleanup passes) that the probe is intentional and
ties it to a tracked issue with rationale for why removal isn't safe
yet. Bare `DBG_TRACE` without the comment is treated as leftover debt
and removed in cleanup.

When the issue closes for real, decide: strip the probe, or convert
the `XXX` to a permanent feature note (drop `DBG-PARKED-N`, keep the
trace as an opt-in diagnostic — but only if it documents a recurring
investigation surface, not a one-off).

## Boundary vs `POSTLOG_MARK`

- `POSTLOG_MARK` (`src/Common/PostLogMark.hpp`) — **always-on**
  instrumentation for production-visible regression markers.
  Examples: `==> ATEXIT fired`, `~CLight_DB canary`, dead-flag
  detection during teardown. Ships in MasterGold. Use sparingly —
  these are landmines from past hangs that we want loud forever.
- `DBG_TRACE_POST` — debug-only sibling. Same stderr fallback (so it
  works after `Core._destroy` closes the engine log), but masked by
  `dbg_mask` and stripped in MasterGold. Use for one-off shutdown
  investigations that don't deserve permanent presence.

Rule of thumb: if the marker only matters when you're actively
debugging a known issue → `DBG_TRACE_POST`. If it's a tripwire for a
regression we never want to ship blind into again → `POSTLOG_MARK`.

## Boundary vs `XXX KEYWORD:` parked probes

The engine has older parked `Msg("XXX KEYWORD: ...")`-style probes
scattered through xrCore/xrEngine/xrGame. These are a different
genre — always-on, category-less, no compile-time strip. They cover
ongoing investigation surfaces that predate this convention.

**Do not bulk-convert.** Each `XXX KEYWORD:` probe is a deliberate
artifact of a past investigation; converting changes semantics
(silent-by-default vs always-firing). If you touch one for an
unrelated reason and want to migrate it, do so individually with a
note in the commit message.

## Anti-patterns

1. **Using `DBG_*` for production telemetry.** If the message needs
   to ship and fire by default, it's `Msg`/`Log`, not `DBG_*`.
   `DBG_*` is silent-by-default and stripped in MasterGold — invisible
   in shipped builds.
2. **Helper functions with category-as-arg.**
   ```cpp
   void dbg_trace_helper(int cat, const char* fmt, ...);  // BAD
   ```
   Defeats compile-strip: the function call survives in MasterGold
   even if its body is empty, and the format strings leak into the
   binary. Always invoke macros directly at the call site.
3. **Per-frame paths without explicit justification.** A `DBG_TRACE`
   inside a render loop or per-tick AI update floods the log even
   when only the relevant category is on. If the probe lives on a
   hot path, either (a) wrap in `DBG_ONCE`, or (b) add an inline
   comment explaining why per-frame noise is acceptable for this
   investigation.

## Quick reference (console)

```
# Enable input pipeline traces for a session
dbg_on input

# Add lifecycle traces on top
dbg_on lifecycle

# See current state
dbg_status

# Wipe everything
dbg_off all

# Direct mask (input + lifecycle = 0x05)
dbg_mask 5
```

Mask is **not persisted** — it resets to 0 every launch. Set it again
in console after boot for each debug session.
