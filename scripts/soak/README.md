# Soak harnesses

Long-running stability tests that drive the engine through repetitive
operations and watch the log for regressions. Used to clear blockers
in [`notes/conventions/done-criteria.md`](../../notes/conventions/done-criteria.md).

## save_load.sh

100×-cycle quicksave/quickload soak. Drives the game via AppleScript
key sends (F5 = save, F9 = load per user keybinds), waits between
cycles, then summarises engine log slice.

**Run:**

1. Launch `OpenXRay-Dev.app`, start a game, get to a known idle
   state (e.g., Yanov station outside, no NPCs nearby).
2. From a Terminal:

   ```bash
   ./scripts/soak/save_load.sh
   ```

   Or with overrides:

   ```bash
   SOAK_CYCLES=20 SOAK_LOAD_DELAY=15 ./scripts/soak/save_load.sh
   ```

3. Don't touch keyboard/mouse during the run. AppleScript needs the
   game window in focus to receive key events.

**Output:**

- Per-run results dir under `_workspace/soak-<timestamp>/`:
  - `run.log` — per-cycle timing.
  - `engine-slice.log` — the engine log lines emitted during the run.
  - `summary.txt` — error counts for `R_ASSERT`, `! ALife save format`,
    `0x502`, shader fails, crashes.

**Pass criteria** (per done-criteria.md):

- 100 cycles complete without crash.
- `R_ASSERT` count = 0.
- `! ALife save format` count = 0.
- No FATAL / segmentation fault lines.
- 0x502 count not significantly higher than baseline (per-run delta
  vs prior `notes/decisions/known-divergence.md` baseline).

**Environment:**

| Var | Default | Notes |
|---|---|---|
| `SOAK_CYCLES` | 100 | Number of save+load round-trips. |
| `SOAK_SAVE_DELAY` | 4 | Seconds to wait after save before triggering load. |
| `SOAK_LOAD_DELAY` | 12 | Seconds to wait after load before next save (level reload time). |
| `SOAK_SAVE_KEY` | 96 | macOS virtual keycode (F5). Override if `bind quick_save` is non-default. |
| `SOAK_LOAD_KEY` | 101 | macOS virtual keycode (F9). Override if `bind quick_load` is non-default. |
| `SOAK_LOG` | `~/Library/Logs/OpenXRay/openxray.log` | Engine log to scrape for errors. |
| `SOAK_APP_NAME` | `OpenXRay-Dev` | Application name for AppleScript focus. |

**Limitations:**

- AppleScript key sends require game window in focus the entire run.
  Don't switch apps. A 100-cycle soak takes ~25 minutes; plan
  accordingly.
- macOS may prompt for accessibility permission the first time
  AppleScript tries to send key events. Approve in
  System Preferences → Privacy → Accessibility for Terminal.
- The script doesn't read back per-cycle save file content — only
  scrapes the engine log. Deserialization drift that doesn't surface
  as a log line won't be caught.

## Future soaks (not yet written)

- `level_transition.sh` — bounce between Janov / Skadovsk / Pripyat
  via map travel, watch for chunk-eviction or smart-terrain reload
  issues.
- `long_play.sh` — 2+ hour idle play, monitor for memory leak / GL
  texture leak / VAO cache growth.
