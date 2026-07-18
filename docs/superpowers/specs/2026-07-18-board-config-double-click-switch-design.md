# Board Configuration Double-Click Switch Design

## Goal

Provide two local double-click entry points that safely switch the exhibition board between the verified compact gimbal-search configuration and the exact original stable-runtime configuration. The tool changes configuration only and never replaces the executable or performs a full runtime restore.

## Files

Create `tools/board-config-switch/` containing:

- `启用紧凑搜索.cmd`: selects the compact configuration.
- `恢复原始旧版配置.cmd`: selects the original stable configuration.
- `switch-board-config.ps1`: shared implementation used by both entry points.
- `board-config-transaction.sh`: board-side transaction implementation uploaded under a unique temporary name.
- `gimbal-board.compact.json`: the already verified compact profile.
- `gimbal-board.original-old.json`: the exact configuration exported from the immutable old-runtime snapshot.
- `known_hosts`: pinned, verified SSH host key for the exhibition board.
- `README.md`: usage, expected board state, and full-runtime fallback location.

The CMD files resolve their own directory, invoke Windows PowerShell with an explicit profile argument, preserve the exit code, print the result, and pause before closing.

## Fixed identity and target

- Default board: `root@192.168.10.125`.
- Expected stable executable SHA-256: `a2c044e5042dd6f185bc7f91fca64b89ee38244f8f784bd07ae169cd33433367`.
- Original config expected SHA-256: `c91bf0d0244c896c4134aad899a0c23899d0c779c944ca143732722a42959eb1`.
- Compact config expected SHA-256: `64ab04d7868be33566ff883e14e0598f4a5d5f4d083c0d77497e92b20528d950`.

The tool has no IP override in its exhibition form. It requires Windows `ssh.exe` and `scp.exe`, uses `BatchMode=yes`, the bundled controlled `known_hosts`, `StrictHostKeyChecking=yes`, and bounded connection/command waits.

## Preconditions

Before mutation, the script must:

1. Confirm the selected local JSON exists, parses successfully, and matches its expected hash.
2. Confirm `ssh.exe` and `scp.exe` exist, then confirm SSH works non-interactively with strict command timeouts and the pinned host key.
3. Fetch `/api/demo/state` and refuse to switch unless the session is inactive and phase is `IDLE`.
4. Confirm the board executable matches the expected stable hash.
5. Read and record the active config hash, ownership, and mode.

These local checks are advisory. The board transaction repeats every identity, idle, and health check while holding its exclusive lock. If the active config already matches the selected profile, the transaction still performs all read-only health gates but does not upload/install a config, restart a service, or touch a radio.

## Safe switch

The PowerShell layer uploads the selected config and transaction script under GUID-based unique names, then starts a uniquely named transient systemd unit. The unit runs the transaction with a total timeout, writes a unique result file, and survives an SSH interruption. PowerShell reconnects and waits for that explicit result.

Inside the transient unit, acquire `/run/lock/edgefocus-config-switch.lock` using non-blocking `flock` and hold it across all checks, mutation, restart, gates, radio recovery, and rollback. A concurrent invocation returns `busy` without mutation.

While holding the lock:

1. Repeat the idle check. Resolve `MainPID`, resolve `/proc/<pid>/exe`, and verify that actual running executable's SHA-256.
2. Require the live config to be a non-symlink regular file under the expected config directory.
3. Stop `focus_app` with a bounded timeout and require it to reach inactive before any configuration replacement. This closes the new-session race and safely ends gimbal control.
4. Create a GUID/`mktemp`-unique backup directory under `/opt/edgefocus/config-switch-backups/`, copy the active config with metadata, and verify its hash matches the recorded pre-switch hash.
5. Validate the uploaded JSON using board-side Python, verify its expected SHA-256, place it in the live config directory on the same filesystem, apply the original owner and mode, sync it, and rename it atomically over `gimbal-board.json`.
6. Re-read the live path and require the target hash.
7. Start `focus_app`, resolve the new `MainPID` and `/proc/<pid>/exe`, and require the stable executable hash again.
8. Confirm parser success only from journal entries belonging to the new PID, preventing an old log line from satisfying the gate.

## Post-switch gates

Require:

- board IPv4 remains present on `wlan0`;
- exactly one `/usr/sbin/udhcpc -f -i wlan0` client remains;
- board-local `/api/demo/state` returns HTTP 200;
- the session remains inactive and `IDLE`;
- `bluetooth.service` is active;
- the BlueZ LE advertisement instance count is one and the new `focus_app` PID logs successful EdgeFocus advertisement registration.

Wait briefly for normal advertisement recovery first. If advertisement remains zero, the same transaction may restart Bluetooth once. Restart `wifi-105-dhcp.service` only if `wlan0` is associated but IPv4 or the default route is actually missing; healthy Wi-Fi must not be touched. Re-run all gates and require exactly one DHCP client. The transaction has a total timeout and never loops radio recovery.

## Automatic rollback

If local/remote validation, atomic install, service startup, parser confirmation, HTTP, or idle-state verification fails after mutation:

1. copy the pre-switch backup with metadata into a same-filesystem temporary path, sync it, and atomically restore it;
2. verify the restored live hash, owner, and mode match the pre-switch values;
3. restart the unchanged stable executable and verify its actual `/proc/<pid>/exe` hash;
4. re-run the service and HTTP gates;
5. report both the original failure and rollback result.

Radio recovery failure is reported distinctly. If the application/configuration gates pass but the radio remains unhealthy after one recovery attempt, do not loop and do not perform repeated Bluetooth restarts. Preserve the pre-switch backup path in the output for manual recovery.

The script never invokes the 880 MB full-runtime restore automatically. That fallback remains at `/root/edgefocus-fallbacks/stable-old-20260718-before-compact-search` with its own `restore.sh`.

## Validation

- Parse both bundled JSON files locally and verify their fixed hashes.
- PowerShell parser-check the shared script.
- Validate both profile paths without mutation using an internal `-ValidateOnly` option that performs no upload, backup, service restart, transient-unit creation, or radio operation.
- Run the compact entry against the board while compact is already active; require an idempotent no-restart success.
- Statically test both CMD files map to the correct fixed profile and preserve the PowerShell exit code after `pause`. CMD uses quoted `%~dp0` and `powershell.exe -NoProfile -ExecutionPolicy Bypass -File ... -Profile <enum>`.
- Test lock contention with a harmless lock-holder transaction and require a second invocation to return `busy` without mutation.
- Provide transaction-script failure injection points after atomic install and after service start; exercise them only against copied temporary config paths or a mock harness, and verify rollback restores hash and metadata.
- Test radio-recovery idempotence structurally and require a unique transaction unit plus exactly one DHCP client gate.
- Do not perform an unnecessary original-to-compact live round trip at the exhibition site.
