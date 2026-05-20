# Updater smoke test (issue #39)

End-to-end test of the in-game "Check for updates" button. Runs entirely
against a local Python HTTP server, no intranet host required.

## Prep

In one terminal, serve dev fixtures:

```sh
mkdir /tmp/xr-updater && cd /tmp/xr-updater
# tiny placeholder asset, content doesn't matter for the transport test
printf 'fake app zip' > dummy.app.zip

# scenario A — newer version on server
cat > manifest-stable.ltx <<'EOF'
[update]
version   = 1.6.fork.test-new
channel   = stable
asset_url = http://127.0.0.1:8000/dummy.app.zip
sha256    = 0000000000000000000000000000000000000000000000000000000000000000
size      = 12
notes     = smoke test scenario A
EOF

python3 -m http.server 8000
```

In another terminal, launch the game (default `updater_manifest_url` already
points at `http://127.0.0.1:8000/manifest-stable.ltx`):

```sh
./bin/arm64/Mixed/xr_3da -fsltx /path/to/fsgame.ltx
```

## Scenarios

| # | Setup | Expected |
|---|-------|----------|
| A | Manifest version `1.6.fork.test-new`, game compiled with default `XRAY_FORK_VERSION=1.6.fork.dev` | Click → `msg_box_new_patch` (yes/no). Yes → progress dialog → `msg_box_patch_download_success`. `pending.app.zip` lands in `~/Library/Application Support/.../updates/` |
| B | Manifest version `1.6.fork.dev` (matches current) | Click → `msg_box_no_new_patch` |
| C | Stop python server, click | Click → `msg_box_patch_download_error` (manifest fetch fails) |
| D | Manifest missing `[update]` section or required fields | Same as C — parser rejects malformed input |
| E | During the download in scenario A, click Cancel | `m_sPDProgress.IsInProgress` → false, ghttp request torn down |

For B, edit `manifest-stable.ltx` and rerun (python `http.server` re-reads
on each request).

For C, `Ctrl+C` the python process and retry the click.

For D, write a manifest body without the `[update]` header — server returns
the body, `CInifile` rejects it, `OnManifestReceived` surfaces
`PatchDownloadError`.

## Log expectations

The engine writes to `~/Library/Logs/OpenXRay/...` (path varies by build).
Look for:

```
updater: checking http://127.0.0.1:8000/manifest-stable.ltx
updater: current=1.6.fork.dev, manifest=1.6.fork.test-new, channel=stable
updater: downloading http://127.0.0.1:8000/dummy.app.zip -> .../pending.app.zip
updater: download complete -> .../pending.app.zip (sha256=0000..., size=12)
```

`Fork version: 1.6.fork.dev` should appear once on engine init (next to the
upstream `OpenXRay ... build NNNN ...` line) — that confirms the
`XRAY_FORK_VERSION` plumbing is live without needing to click anything.

## Console overrides

- `updater_manifest_url <url>` — point the check at a different host (real
  intranet endpoint, alternate dev port, etc).
- `check_for_updates` — runs the check programmatically (same path as the
  UI button).

## Out of scope for this smoke

- SHA256 verification — manifest field is read but not enforced yet
  (USE_OPENSSL undefined in CMake builds; CommonCrypto wiring is a
  follow-up sub-issue).
- Atomic install — "restart now" prompt acknowledges the pending file but
  the user manually replaces `.app`. Atomic swap helper is a follow-up.
- Manifest signing — VPN-only scope makes this nice-to-have; backlog issue.

## Boundaries of this dev fixture

The `manifest-stable.ltx` here is a hand-crafted local file. When the
intranet host is configured, the same console cvar swap (`updater_manifest_url
http://updates.vg.lan/...`) plus a real manifest on the server completes the
deployment without any client rebuild.
