# Board Configuration Double-Click Switch Design

## Goal

Provide two local double-click entry points that safely switch the exhibition board between the verified compact gimbal-search configuration and the exact original stable-runtime configuration. The tool changes configuration only and never replaces the executable or performs a full runtime restore.

## Files

Create `tools/board-config-switch/` containing:

- `启用紧凑搜索.cmd`: selects the compact configuration.
- `恢复原始旧版配置.cmd`: selects the original stable configuration.
- `switch-board-config.ps1`: shared implementation used by both entry points.
- `gimbal-board.compact.json`: the already verified compact profile.
- `gimbal-board.original-old.json`: the exact configuration exported from the immutable old-runtime snapshot.
- `README.md`: usage, expected board state, and full-runtime fallback location.

The CMD files resolve their own directory, invoke Windows PowerShell with an explicit profile argument, preserve the exit code, print the result, and pause before closing.

## Fixed identity and target

- Default board: `root@192.168.10.125`.
- Expected stable executable SHA-256: `a2c044e5042dd6f185bc7f91fca64b89ee38244f8f784bd07ae169cd33433367`.
- Original config expected SHA-256: `c91bf0d0244c896c4134aad899a0c23899d0c779c944ca143732722a42959eb1`.
- Compact config expected SHA-256: `64ab04d7868be33566ff883e14e0598f4a5d5f4d083c0d77497e92b20528d950`.

The PowerShell implementation may accept an optional board IP override for engineering use, but double-click entry points use the fixed exhibition address.

## Preconditions

Before mutation, the script must:

1. Confirm the selected local JSON exists, parses successfully, and matches its expected hash.
2. Confirm SSH works non-interactively with strict command timeouts.
3. Fetch `/api/demo/state` and refuse to switch unless the session is inactive and phase is `IDLE`.
4. Confirm the board executable matches the expected stable hash.
5. Read and record the active config hash, ownership, and mode.

If the active config already matches the selected profile, report success without restarting any service or radio.

## Safe switch

1. Copy the active config to a timestamped file under `/opt/edgefocus/config-switch-backups/`.
2. Upload the selected configuration to a temporary file in the live config directory.
3. Validate the uploaded JSON using board-side Python and verify its SHA-256.
4. Apply the active config's owner and mode to the candidate.
5. Rename the candidate atomically over `gimbal-board.json`.
6. Restart the unchanged `focus_app` service.
7. Require `focus_app` to become active and its log to confirm `Gimbal capability enabled with verified board config`.

## Post-switch gates

Require:

- board IPv4 remains present on `wlan0`;
- exactly one `/usr/sbin/udhcpc -f -i wlan0` client remains;
- board-local `/api/demo/state` returns HTTP 200;
- the session remains inactive and `IDLE`;
- `bluetooth.service` is active;
- the BlueZ LE advertisement instance count is one.

Wait briefly for normal advertisement recovery first. If advertisement remains zero, launch one detached transient systemd recovery job that restarts Bluetooth, waits, then restarts the existing `wifi-105-dhcp.service`. Reconnect and re-run all gates. Do not start a second DHCP client.

## Automatic rollback

If local/remote validation, atomic install, service startup, parser confirmation, HTTP, or idle-state verification fails after mutation:

1. restore the timestamped pre-switch configuration atomically;
2. restart the unchanged stable executable;
3. re-run the service and HTTP gates;
4. report both the original failure and rollback result.

Radio recovery failure is reported distinctly. If the application/configuration gates pass but the radio remains unhealthy after one recovery attempt, do not loop and do not perform repeated Bluetooth restarts. Preserve the pre-switch backup path in the output for manual recovery.

The script never invokes the 880 MB full-runtime restore automatically. That fallback remains at `/root/edgefocus-fallbacks/stable-old-20260718-before-compact-search` with its own `restore.sh`.

## Validation

- Parse both bundled JSON files locally and verify their fixed hashes.
- PowerShell parser-check the shared script.
- Validate both profile paths without mutation using an internal `-ValidateOnly` option.
- Run the compact entry against the board while compact is already active; require an idempotent no-restart success.
- Do not perform an unnecessary original-to-compact live round trip at the exhibition site.
