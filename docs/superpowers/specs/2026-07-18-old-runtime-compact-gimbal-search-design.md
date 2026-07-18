# Old Runtime Compact Gimbal Search Design

## Goal

Shorten the automatic PRECHECK search of the currently stable old runtime without replacing or rebuilding its executable. Preserve a complete, independently restorable snapshot of that known-good runtime as the exhibition fallback.

## Scope

- Keep the active old `focus_app` executable unchanged.
- Change only the active old runtime's `config/gimbal-board.json` search parameters.
- Do not change App, BLE, web UI, visual inference, session semantics, tracking, manual nudge, or system services.
- Do not deploy the newer candidate executable.

## Preserved fallback

Before changing the active configuration, create a timestamped immutable-style snapshot outside the live `/opt/edgefocus` tree containing:

- a metadata-preserving archive of the complete active `/opt/edgefocus` tree, excluding only the existing `releases/` backup tree to avoid recursion;
- the effective `focus_app.service` unit, its source unit file, all drop-ins, and any referenced `EnvironmentFile` files;
- a manifest containing source paths, file types, symbolic-link targets, ownership, permissions, sizes, timestamps, and SHA-256 hashes for regular files;
- a short restore script or exact restore commands stored with the snapshot.

The snapshot must be stored under a unique timestamped name, must not be used as the working deployment target, and must not be overwritten by later tests. The existing rollback copies remain untouched as a second fallback. Verify available storage before creating the archive and verify the archive can be listed after creation.

## Configuration change

Retain all old-parser-required fields, including `effective_vertical_fov_deg` and `full_pitch_recovery_extent_deg`. Change only these values in the `search` object:

| Field | New value | Purpose |
| --- | ---: | --- |
| `yaw_min` | `-30.0` | limit automatic horizontal sweep |
| `yaw_max` | `30.0` | limit automatic horizontal sweep |
| `pitch_min` | `-15.0` | constrain any queued vertical target |
| `pitch_max` | `15.0` | constrain any queued vertical target |
| `verified_min_speed_deg_s` | `40.0` | compute compact waypoint dwell deadlines |
| `initial_observe_ms` | `400` | quickly accept a person already in view |
| `stop_queueing_ms` | `6000` | stop before the old parser enters vertical/recovery points |
| `hard_deadline_ms` | `9000` | final search cutoff |

The old parser still constructs seven primary and several recovery waypoints. With its minimum 3-second waypoint dwell, this configuration admits the first two horizontal waypoints only: approximately `-30°`, then `+30°`, followed by the old runtime's existing return-to-center behavior. `stop_queueing_ms` is checked before either a primary or recovery waypoint is selected, so a later scheduler tick can reduce the number of movements but cannot enqueue a third or recovery waypoint after the six-second cutoff. Recovery parameters remain parser-valid but their waypoints are not expected to execute.

The old `finishSearch()` performs a synchronous verified return to center with an eight-second command timeout. Therefore `hard_deadline_ms=9000` is a search-admission cutoff, not a wall-clock interrupt for an already-running return-to-center command. Normal completion is expected around 8–11 seconds; a conservative acceptance ceiling is 16 seconds.

## Deployment

1. Verify the board is running the known-good old executable and is idle.
2. Record the active executable SHA-256, then create and verify the complete fallback snapshot.
3. Produce the compact config as a separate candidate file and validate it as JSON.
4. Install the candidate under a temporary file in the live config directory, preserve the original owner and mode, and rename it atomically over `gimbal-board.json`.
5. Restart the unchanged old executable. Its startup must log `Gimbal capability enabled with verified board config`; this is the semantic validation performed by the exact deployed parser. If validation fails, restore before starting a session.
6. Do not restart Bluetooth unless advertisement recovery is actually required. If Wi-Fi remains associated but loses IPv4, restart the existing `wifi-105-dhcp.service`; do not start a second DHCP client.

## Acceptance test

Use the real web session API, because it exercises the same path as the exhibition page:

1. Confirm idle state, HTTP responsiveness, Wi-Fi IPv4, SSH, Bluetooth service, and one BLE advertisement instance.
2. Start a one-minute formal session.
3. Observe PRECHECK transitions and measure state endpoint latency.
4. For the no-subject search test, inspect the board command log and physical motion: expect the `-30°` and `+30°` horizontal targets followed by return to center, with no third primary or recovery target. A scheduler-delayed run may stop after one target and is still safe, but a third or recovery target is a failure. A separate subject-present run may terminate early and does not validate the complete no-subject sequence.
5. Expect manual adjustment availability approximately within 8–11 seconds. Allow up to 16 seconds for the old runtime's synchronous verified return to center.
6. End the session and confirm the board returns to idle.
7. Recheck HTTP, Wi-Fi, BLE advertisement, and service health.

## Rollback conditions

Immediately restore the complete known-good snapshot configuration and restart the old runtime if any of the following occurs:

- automatic PRECHECK has not released to manual adjustment within 16 seconds;
- the process, web endpoint, Wi-Fi, BLE advertisement, or SSH becomes unhealthy;
- the gimbal enters degraded/fault state;
- the search reaches recovery scanning or moves outside the intended compact range;
- configuration validation or service startup fails.

Rollback restores the complete archived runtime tree and captured systemd material with metadata, runs `systemctl daemon-reload` if unit material was restored, and verifies the active executable and configuration hashes against the manifest. It then rechecks service state, HTTP, idle/session start, Wi-Fi IPv4, SSH, and BLE advertisement.
