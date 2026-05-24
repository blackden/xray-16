# Save format versioning — discovery and design

> **SUPERSEDED IN PART — read `save-format-audit.md` (same dir) first.**
>
> An audit done after this doc was written (2026-05-19) showed the engine
> already has substantial forward-compat machinery:
> per-CSE `m_wVersion` field-gating and top-level chunked layout. The
> "migration framework" described below is overkill for ~90% of typical
> schema changes — adding fields to `CSE_*` only needs an
> `if (m_wVersion > N)` branch, no migrator. Demoted from P1 to P3.
> The audit doc lists the smaller, more useful next steps (~1-2 days vs
> 5-7 days).
>
> The original design below is kept for reference — the migration-chain
> pattern is still useful for cross-chunk restructuring (Case A/B in the
> audit doc), just not the default workflow.

## Why this matters

OpenXRay's save format is binary, position-tied, and **rejects any save
older than current `ALIFE_VERSION`** (see
`src/xrGame/alife_simulator_header.cpp:24`:
`R_ASSERT2(m_version >= ALIFE_VERSION, "ALife version mismatch! (Delete saved game and try again)");`).

Every time we change a serializable struct — adding a field to
`CSE_ALifeHumanAbstract`, changing the layout of `Actor` state,
extending inventory item flags — we have to bump `ALIFE_VERSION`, and
every existing player save **silently dies on next load**.

This is the highest-impact latent risk in the engine for any future
ALife extension work. P2 in `../strategy/roadmap.md` strategic direction
(decisions.md #17 onwards). Doing it once, properly, unlocks safe
state-schema evolution forever.

## Current state — what we know

### Save file layout (high-level, from grep walks)

A typical CoP save in `$game_saves$` is roughly:

```
[u32 alife_version]                         # CALifeSimulatorHeader::load
[u32 m_guid; shared_str m_game_name; ...]   # rest of header
[chunk: spawn_registry]                     # CSpawnRegistry
[chunk: object_registry]                    # alife_object_registry, sequence of CSE_*
[chunk: graph_registry]                     # alife_graph_registry
[chunk: group_registry]                     # alife_group_registry
[chunk: registry_container]                 # alife_registry_container (lua state, vars)
[chunk: time_manager]                       # alife_time_manager
... game-specific tails ...
```

Some sections use the chunk pattern (`{chunk_id:u32, size:u32, payload}`),
some don't — they're raw concatenated bytes that depend on read order
matching write order **byte-for-byte**.

### Where serialization actually happens

- `CALifeStorageManager::save` / `::load` — `src/xrGame/alife_storage_manager.cpp:29-180`
  — top-level entry points; calls `header().save()` then dispatches into
  the registries.
- `CALifeObjectRegistry` — iterates ALife objects, calls
  `CSE_ALifeDynamicObject::STATE_Read/Write` on each. Position-tied per
  class hierarchy.
- `CSE_*` classes — `xrServerEntities/xrServer_Objects_ALife*.cpp` —
  each class has its own `STATE_Read(IReader&)` / `STATE_Write(NET_Packet&)`
  doing field-by-field appends. **Adding a field at the end** is the
  cheap path (vanilla format remains a prefix); **inserting in the
  middle** breaks everything downstream.
- `CGameObject` (runtime state when an entity is online) — `src/xrGame/`
  many .cpp files; this is the "transient at-runtime" state that gets
  written into the save on autosave.

### Already-shipped legacy migration

There's a `cp1251 → UTF-8` shim in `load_data` for save filenames /
character names — a one-off transcoding hook added during UTF-8
migration (Phases 0-4). It does the job for that specific concern but
**doesn't generalise** to schema migration; it's a string-encoding
hack at the bytes level, not a versioned migrator.

## Design — proposed approach

### Goal

> Save written by binary version V can be loaded by binary version W
> (W > V), with bounded migration cost, without manual user action.

Don't aim at "any version → any version" — only **forwards
compatibility** (load older saves into newer engine). The reverse
direction (load newer saves into older engine) is out of scope; players
who downgrade can't reasonably expect this.

### Mechanism: migration chain

Introduce a per-version-bump migration function:

```cpp
// One function per (N) → (N+1) bump. Reads a save serialized at v_N
// and writes the same logical state in v_(N+1) format.
//
// Signature deliberately raw IReader/IWriter — migrations are
// byte-stream rewrites, not C++ struct-level operations, because the
// in-memory class shape *of the current code* doesn't match v_N's
// serialized layout (otherwise we wouldn't need the migration).
namespace save_migrations
{
    bool migrate_v_120_to_121(IReader& src, IWriter& dst);
    bool migrate_v_121_to_122(IReader& src, IWriter& dst);
    // ...
}
```

### Loader pipeline

In `CALifeStorageManager::load`:

```cpp
IReader* source = FS.r_open(save_path);

CALifeSimulatorHeader peek_header;
peek_header.load(*source);
u32 file_version = peek_header.version();
source->seek(0);

if (file_version < ALIFE_VERSION)
{
    // Build a chain of in-memory IWriter -> IReader buffers, applying
    // one migration per version step. Cap at e.g. 32 steps so a
    // malformed save can't loop forever.
    IReader* migrated = apply_migration_chain(source, file_version, ALIFE_VERSION);
    FS.r_close(source);
    source = migrated;
}

// Now source is at current ALIFE_VERSION — proceed as today.
header().load(*source);
// ... rest unchanged ...
```

### Migration function characteristics

Each `migrate_v_N_to_v_N+1` is **write-once**, never modified later.
Once shipped, it stays as-is. This is critical: if you change a
migration retroactively, saves migrated through the old version are
now subtly different from saves migrated through the new version.
That's a debugging nightmare.

Migrations live in `src/xrGame/save_migrations/v_NNN_to_v_NNN_plus_1.cpp`.
Tests pin one or two real saves per version (committed as fixtures in
`tests/saves/`) and exercise full chain.

### What "bump ALIFE_VERSION" workflow looks like

When you make a serialization change:

1. Bump `ALIFE_VERSION` from N to N+1.
2. Add `migrate_v_N_to_v_N+1(IReader& src, IWriter& dst)`. The function
   reads the old layout (which is, by definition, what the *previous*
   `STATE_Read` did for the changed types) and writes the new layout
   (which is what *current* `STATE_Read` expects).
3. Add one save file to `tests/saves/v_N/` as a regression fixture.
4. Add a check in `tests/regression_checks.sh` that the v_N save loads
   in current engine.

The expensive part is step 2 — writing the migration. But this cost
is **paid once per bump**, vs current cost of **every player loses
their saves every bump**. Net win after ~3 bumps.

### Compatibility surface

For the *first* version of this scheme (ALIFE_VERSION = N_0, current),
there are no migrations to write — we just commit the loader pipeline
plumbing and the empty migration table. The first time we bump version
*after* this lands, we write the first migration.

This means **the cost is amortised over future work**, not paid up-front
as a giant retroactive migration. Existing saves stay loadable today
because nothing's changed.

## Risks / open questions

- **Chunk-based vs position-tied sections.** Some sections of the save
  use the chunk pattern (recoverable from unknown chunks); some are
  raw byte concatenation (any layout mismatch corrupts everything
  downstream). Migrations have to know which is which. Audit:
  enumerate every `*::STATE_Read` callsite and classify.
- **Lua-side state.** `alife_registry_container` includes Lua values
  saved by scripts via a custom binary serializer. Schema changes
  there are script-driven, not engine-driven. Need a parallel Lua-side
  versioning hook — out of scope for v1 of this design.
- **Multi-byte field changes within a struct.** If `STATE_Write` of
  `CSE_ALifeMonsterAbstract` used to write a `u8 mood` and now writes
  a `u16 mood`, the migration has to rewrite that one field in the
  middle of the per-object stream — and CSE objects don't have their
  own chunk boundaries (they're concatenated by the registry). This
  is doable but requires per-class knowledge in the migration.
- **Performance.** Migration runs in-process at load time. For a long-
  alife save (50K objects), copying through `migrate_v_N_to_v_N+1` is
  O(save_size) per step. Should be fine — saves are 1-10 MB typically —
  but worth measuring after first migration ships.

## Implementation effort estimate

- **First-pass scaffold (loader pipeline + empty migration table +
  apply_migration_chain).** 2-3 days.
- **Per migration.** 0.5-2 days each, depending on what changed.
- **Test infrastructure (fixture saves + per-version regression check).**
  1 day.
- **Audit of existing STATE_Read/Write to know which classes are
  position-tied vs chunked.** 1-2 days.

**Total first version: ~5-7 working days** to ship the framework with
no migrations registered. Future schema changes carry their own
~1-day migration cost each.

## Adjacent improvements (separate work)

- **Compression.** Saves are uncompressed binary; gzip/lzo would halve
  load time on disk-bound machines. Independent of versioning.
- **JSON or msgpack save format.** True schema-level format change.
  Out of scope here — that's a different P-N project.
- **Save-format-aware migration tooling (CLI).** "Take a saved game
  written at v_N, output v_M version." Useful for testing migrations
  externally without launching the game.

## Files to be touched (when implementing)

- `src/xrGame/alife_storage_manager.cpp` — add migration pipeline call
  before `header().load()`.
- `src/xrGame/alife_simulator_header.cpp` — relax the
  `R_ASSERT2(m_version >= ALIFE_VERSION)` check; new behaviour is "if
  newer, fail; if older, attempt migration".
- New: `src/xrGame/save_migrations/` directory with one file per
  version step.
- New: `src/xrGame/save_migrations/registry.h/.cpp` — the migration
  function table.
- `tests/saves/` — committed save fixtures.
- `tests/regression_checks.sh` — per-version load checks.

## Next concrete action when picked up

1. Audit `STATE_Read/Write` callsites — produce a list of every class
   that participates in save serialization. ~1 day with grep + careful
   reading.
2. Read 2-3 historical CoP / OpenXRay saves with a hex viewer; document
   actual layout (chunks vs concat).
3. Implement scaffold (loader pipeline + empty registry).
4. Ship. Then the next time we'd otherwise bump `ALIFE_VERSION`, write
   the first migration — which lets us validate the design on real work.
