# Save format audit — findings

Audit done while pulling up the migration design doc
(`save-format-versioning.md`). Major findings here update the design.

## TL;DR

The save format is **already substantially forward-compat** by design.
The migration-framework approach I sketched in
`save-format-versioning.md` was overkill — for ~90% of schema-change
cases the existing per-object `m_wVersion` gating is sufficient and
requires zero migration code. The remaining ~10% (cross-chunk
restructuring) is the only place a real migrator helps. **Recommendation:
demote P1 from "big framework rewrite" to "policy doc + tightening".**

## The three layers of versioning that already exist

### Layer 1 — top-level chunked format

`alife_storage_manager` writes/reads several **chunks**, each addressed
by a numeric ID and discovered via `find_chunk`:

| Chunk | Owner | File |
|-------|-------|------|
| `ALIFE_CHUNK_DATA` | header (`u32 ALIFE_VERSION`, GUID, time, etc.) | `xrGame/alife_simulator_header.cpp:15` |
| `SPAWN_CHUNK_DATA` | spawn registry, with nested sub-chunks 0..4 | `xrGame/alife_spawn_registry.cpp:35` |
| `OBJECT_CHUNK_DATA` | per-object stream (CSE_* concat inside) | `xrGame/alife_object_registry.cpp:62` |
| `REGISTRY_CHUNK_DATA` | lua-side state | `xrGame/alife_registry_container.cpp:48` |
| `GAME_TIME_CHUNK_DATA` | time manager | `xrGame/alife_time_manager.cpp:31` |

Because they're chunks, **the loader can skip unknown chunks and load
known ones**. Order-independence at the top level is built in. Adding a
new top-level chunk is non-breaking; old engines just ignore it. The
only non-extensible boundary is the **content of each chunk**.

### Layer 2 — `ALIFE_VERSION` (`alife_space.h:14`)

Single `u32`, currently `0x0007`. Stored at the start of
`ALIFE_CHUNK_DATA`. Loaded in `CALifeSimulatorHeader::load` at
`alife_simulator_header.cpp:24`:

```cpp
R_ASSERT2(m_version >= ALIFE_VERSION, "ALife version mismatch! ...");
```

History (via `git log -p` on `alife_space.h`):

- Bumped from 6 → 7 **once**, commit `c08110422`. That's the only bump
  in OpenXRay's history.

Two observations:

1. The check is `m_version >= ALIFE_VERSION` — meaning **newer saves
   load in older engines** (suspicious!) and **older saves are
   rejected**. The polarity is wrong for the typical "be tolerant of
   what you read" rule. If a future-version save lands in current
   engine, the read just keeps going and probably parses garbage.
2. The version has been static for so long that no developer
   experience exists for "what happens when we bump it" — meaning the
   framework around it (`R_ASSERT2 →` crash) is the only behaviour
   anyone has ever observed.

### Layer 3 — per-CSE `m_wVersion` (`xrServer_Object_Base.cpp:254`)

Each `CSE_*` object writes its own `u16 m_wVersion` (gated by
`s_flags.is(M_SPAWN_VERSION)` for back-compat with very old saves).
Every subclass then **field-gates** with `if (m_wVersion > N)`:

```cpp
// xrServer_Objects_ALife_Monsters.cpp:1113
if (m_wVersion > 72)
    tNetPacket.r_stringZ(m_out_space_restrictors);

if (m_wVersion > 111)
    tNetPacket.r_u16(m_smart_terrain_id);
```

`m_wVersion` values seen in current code range from ~19 to ~125. **This
is the real forward-compat mechanism.** A typical schema change
("add `u32 mood` to monster") looks like:

1. Bump some local `m_wVersion` constant (or piggyback on the next
   open one).
2. Add a new branch: `if (m_wVersion > N) tNetPacket.r_u32(mood);`.
3. On write, always emit at the new version.
4. Old saves (with `m_wVersion < N`) parse fine — `mood` stays
   default-initialised. No migration needed.

Coverage: this exists in **all CSE_* hierarchies** I sampled
(`xrServer_Objects_ALife_Monsters.cpp`,
`xrServer_Objects_ALife_Items.cpp`,
`xrServer_Objects_ALife.cpp`). ~380 STATE_Read/Write references across
15 files, almost universally guarded.

## Where the framework actually breaks

Given Layers 1-3, the real "saves get rejected" cases are:

### Case A — `ALIFE_VERSION` bump

Anyone bumping `ALIFE_VERSION` from 7 → 8 immediately breaks every
existing save via the `R_ASSERT2` at `alife_simulator_header.cpp:24`.
This happens when the change is cross-chunk or affects the *meaning*
of an existing field, not just its presence (rare).

### Case B — non-CSE chunk content changes

Inside `SPAWN_CHUNK_DATA`, `REGISTRY_CHUNK_DATA`, `GAME_TIME_CHUNK_DATA`
the content is position-tied. Layout changes there require either:
- A new sub-chunk ID (back-compat), or
- A migration.

Worth noting: `alife_registry_container` houses Lua state, which is
script-defined and totally outside engine control. That's its own
versioning problem.

### Case C — direct removal of an old `m_wVersion > N` branch

If someone deletes an `if (m_wVersion > 35) ...` branch because they
"don't support v6 saves anymore", saves with `m_wVersion in (35..N)`
silently corrupt: the stream position no longer matches. This is the
**main hazard** of long-term maintenance.

## Revised priority assessment

**Original P1 (save format versioning framework):** "5-7 days scaffold",
designed assuming saves are linearly versioned binary blobs with no
internal mechanism.

**Reality:** The mechanism exists. What's missing:

1. **A policy doc** that says: "Adding fields to CSE_* is safe; bumping
   `ALIFE_VERSION` is destructive; deleting `m_wVersion` branches
   destroys old-save compat."
2. **A test** that pins down "load this old save fixture, dump it,
   compare to expected dump." Defends against Case C creeping in.
3. **Soft-fail of `R_ASSERT2(m_version >= ALIFE_VERSION)`** — fail
   gracefully with a clear UI error rather than crashing into the
   debugger. Tiny patch, high user value.
4. **(Optional)** A migration helper for Case A/B — written once,
   exercised only when needed. Lower priority than (1)-(3).

This is a ~1-2 day P2 effort, not a 5-7 day P1 framework. **Demote P1
to P3.**

## Concrete next steps

1. Update `save-format-versioning.md` to reflect the revised
   understanding (or supersede it).
2. Patch `alife_simulator_header.cpp:24` to log + fail-soft instead of
   `R_ASSERT2`. Output a user-readable error string. ~30 min.
3. Add a regression test: load a committed CoP save fixture
   (`tests/saves/cop_v7.sav`), assert it loads without modification.
   Defends Case C. ~2-3 hours including capturing the fixture.
4. Write a SAVES.md style note in `doc/` (or `notes/saves.md`) for
   future contributors: how to add fields safely, what bumps mean,
   how to test compat.

## Files / locations referenced

- `src/xrServerEntities/alife_space.h:14` — `ALIFE_VERSION` macro.
- `src/xrGame/alife_simulator_header.cpp:20-25` — the assert.
- `src/xrGame/alife_storage_manager.cpp:29-180` — top-level entry points.
- `src/xrGame/alife_object_registry.cpp:60-150` — CSE iteration.
- `src/xrGame/alife_spawn_registry.cpp:35-150` — nested chunks 0..4 in
  spawn.
- `src/xrServerEntities/xrServer_Object_Base.cpp:240-290` — `CSE_Abstract::STATE_Read` + version field.
- `src/xrServerEntities/xrServer_Objects_ALife_Monsters.cpp:188-260`
  — exemplar of dense `m_wVersion > N` gating.
- Commit `c08110422` — the only historical `ALIFE_VERSION` bump (6→7).
