# Save-format policy — touching `STATE_Read/Write` and `ALIFE_VERSION`

Boundary doc for anyone (us included, next month) who's about to
change a CSE class layout, add a new field, or bump `ALIFE_VERSION`.
Based on `notes/save-format-audit.md` — read that for the full
reasoning. This doc is the **checklist** distilled to "what do I do
when…".

## The four cases

From the audit, every save-format change falls into one of these.
Pick the right one before touching code.

### Case A — adding a new field at the end of an existing CSE class

**Safe via Layer 3 (`m_wVersion`) gating without bumping
`ALIFE_VERSION`.**

In `STATE_Read`:

```cpp
if (m_wVersion > VERSION_OF_LAST_CHANGE)
{
    F.r_<type>(new_field);
}
else
{
    new_field = DEFAULT_VALUE;
}
```

In `STATE_Write` — write unconditionally. The version stamp comes from
`CSE_Abstract::m_wVersion` and gets bumped in `xrServer_Objects_*.cpp`
when the class changes. Old saves with smaller `m_wVersion` skip the
new field; new saves with current `m_wVersion` read it.

**Do not bump `ALIFE_VERSION`** — that's reserved for top-level
chunked layout changes.

### Case B — changing semantics of an existing field

Same as Case A, but **bump the local `m_wVersion`** in
`xrServer_Objects_*.cpp` and gate the new semantic behind it. Keep
the old read path under `m_wVersion <= OLD_VERSION` so old saves
still load.

```cpp
if (m_wVersion >= NEW_VERSION)
{
    // new format
}
else
{
    // legacy interpretation -- compute equivalent value
}
```

### Case C — removing an old `m_wVersion` branch

**Don't.** Every `if (m_wVersion > N)` ladder is what keeps multi-year-
old saves loadable. Removing one breaks anyone who saved before that
bump.

Exception: if `ALIFE_VERSION` is bumped (Case D), all `m_wVersion`
ladders below the floor it implies can be collapsed in the same
commit. Mention it in the commit message.

### Case D — bumping `ALIFE_VERSION`

The "we don't load saves older than this" floor. Used **once** in
OpenXRay history (6→7). Reserve for:
- Restructuring top-level chunks (rename, split, merge).
- Removing a `CSE_*` class entirely such that an old object record
  can't be skipped.
- Changing the on-wire binary format of a primitive (e.g. u16 → u32).

**Don't bump for:** adding fields (use Case A), adding new CSE classes
(loader ignores unknown class IDs in the spawn registry).

Before bumping:
1. Run the regression test (see below) against the previous
   `ALIFE_VERSION`. It MUST fail clearly with the
   "ALife save format mismatch" message — that's the user-visible
   failure mode for old saves.
2. Update `tests/fixtures/cop_v<new_version>.scop` to match the new
   format. Keep the old fixture as `cop_v<old_version>.scop` for the
   "should fail" branch of the test.
3. Document the breakage in the commit message.

## Checklist before merging a PR that touches `STATE_Read/Write`

- [ ] Decided which case (A/B/C/D) this falls under.
- [ ] If Case A/B: `m_wVersion` bumped in the matching class's
      `STATE_Write`.
- [ ] If Case C: don't.
- [ ] If Case D: regression test updated; user-visible message
      reviewed.
- [ ] `make test` runs `save_format_regression_test` (TODO once the
      test ships) and it passes.
- [ ] Loaded one of the user's actual saves on the changed branch.
      (Manual; no automated way until we ship a save corpus.)

## Diagnostic improvements (now)

`CALifeSimulatorHeader::load` now logs the actual version numbers
before asserting on mismatch (`alife_simulator_header.cpp:24-37`).
When users report "save won't load," the OpenXRay log immediately
shows file_version vs engine_version. Previously you had to attach a
debugger.

Example of the log line:

```
! ALife save format mismatch: save has version 6 (0x6), engine requires
  >= 7 (0x7). Delete the save and start a new game, or load with an
  older build.
```

## Future work (deferred)

- Wire `CALifeSimulatorHeader::valid()` into the **load-savegame UI**
  so the user gets a polite "save too old" toast instead of the
  R_ASSERT crash dialog. Currently `valid()` exists
  (`alife_simulator_header.cpp:27`) but has no callers. Estimate:
  ~½ day. Out of scope for the current pass.
- Migration helper for Case D (`migrate_v6_to_v7(...)` style) — not
  needed yet. Add when we hit a real upgrade scenario.

## Pointers

- Audit reasoning: `notes/save-format-audit.md`.
- Original design (now overscoped): `notes/save-format-versioning.md`.
- `ALIFE_VERSION` define: `src/xrServerEntities/alife_space.h:14`.
- `STATE_Read/Write` base: `src/xrServerEntities/xrServer_Object_Base.cpp`.
- Per-class versioning examples: `src/xrServerEntities/xrServer_Objects_ALife_Monsters.cpp`.
- Top-level chunk loader: `src/xrGame/alife_storage_manager.cpp:128-180`.
