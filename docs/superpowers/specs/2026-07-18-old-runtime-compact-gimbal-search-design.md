# Old Runtime Compact Gimbal Search Design

## Goal

Shorten the automatic PRECHECK search of the currently stable old runtime without replacing or rebuilding its executable. Preserve a complete, independently restorable snapshot of that known-good runtime as the exhibition fallback.

## Scope

- Keep the active old `focus_app` executable unchanged.
- Change only the active old runtime's `config/gimbal-board.json` search parameters.
- Do not change App, BLE, web UI, visual inference, session semantics, tracking, manual nudge, or system services.
- Do not deploy the newer candidate executable.

## Preserved fallback

Before changing the active configuration, create a timestamped immutable-style snapshot under `/opt/edgefocus/releases/` containing:

- the active `focus_app` executable;
- the complete active `config/` directory;
- the active `focus_app.service` unit;
- a manifest containing source paths, sizes, timestamps, and SHA-256 hashes;
- a short restore script or exact restore commands stored with the snapshot.

The snapshot directory must not be used as the working deployment target and must not be overwritten by later tests. The existing rollback copies remain untouched as a second fallback.

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

The old parser still constructs seven primary and several recovery waypoints. With its minimum 3-second waypoint dwell, this configuration admits the first two horizontal waypoints only: approximately `-30°`, then `+30°`, followed by the old runtime's existing return-to-center behavior. Recovery parameters remain valid but their waypoints should not be reached.

## Deployment

1. Verify the board is running the known-good old executable and is idle.
2. Create and verify the complete fallback snapshot.
3. Produce the compact config as a separate candidate file and validate it as JSON.
4. Install only the candidate config and restart `focus_app`.
5. Do not restart Bluetooth unless advertisement recovery is actually required. If Wi-Fi remains associated but loses IPv4, restart the existing `wifi-105-dhcp.service`; do not start a second DHCP client.

## Acceptance test

Use the real web session API, because it exercises the same path as the exhibition page:

1. Confirm idle state, HTTP responsiveness, Wi-Fi IPv4, SSH, Bluetooth service, and one BLE advertisement instance.
2. Start a one-minute formal session.
3. Observe PRECHECK transitions and measure state endpoint latency.
4. Confirm automatic search performs no more than the two intended horizontal sweeps before returning to manual adjustment.
5. Require manual adjustment availability approximately within 8–11 seconds and no later than 12 seconds under normal hardware operation.
6. End the session and confirm the board returns to idle.
7. Recheck HTTP, Wi-Fi, BLE advertisement, and service health.

## Rollback conditions

Immediately restore the complete known-good snapshot configuration and restart the old runtime if any of the following occurs:

- automatic PRECHECK has not released to manual adjustment within 12 seconds;
- the process, web endpoint, Wi-Fi, BLE advertisement, or SSH becomes unhealthy;
- the gimbal enters degraded/fault state;
- the search reaches recovery scanning or moves outside the intended compact range;
- configuration validation or service startup fails.

Rollback restores the snapshot's executable and complete `config/` directory together, then verifies their hashes against the manifest.
