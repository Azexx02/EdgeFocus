# H.264 preview and manual gimbal takeover repair

Date: 2026-07-22
Status: Approved

## Goals

1. Replace the live browser preview transport with low-latency AVC/H.264 while preserving the existing latest-JPEG path as an automatic fallback.
2. Keep camera capture and NPU inference authoritative and independent from preview encoding or browser speed.
3. Repair Web and Android PRECHECK manual-control ownership so skip, takeover, nudge, stop, and confirm remain usable across asynchronous board state updates.

## Verified constraints

- The USB camera exposes MJPEG and YUYV, not H.264.
- Board FFmpeg advertises `h264_v4l2m2m`, but a real encode fails with `Could not find a valid device`; there is no usable hardware encoder node.
- Board GStreamer lacks `webrtcbin`, `v4l2h264enc`, `openh264enc`, and `x264enc`.
- The exhibition page is served over ordinary HTTP. Chromium exposes MSE with AVC support there, but not WebCodecs.
- The current Canvas/latest-JPEG path is stable enough to remain the fail-safe.

## Video design

`CameraPipeline` remains the only camera owner. `PreviewFrameStore` publishes the newest frame to two independent, bounded consumers:

- JPEG encoder: existing latest-frame response and fallback.
- H.264 encoder: OpenH264 software encoder, 640x480, 15 FPS target, constrained baseline, one-second GOP, no B frames, bitrate target about 1.2 Mbit/s.

The H.264 branch converts the newest BGR/RGB frame to I420, never queues more than one pending source frame, and emits fragmented MP4 suitable for MSE. A slow browser cannot back-pressure camera capture or NPU inference. The browser keeps a short live window, seeks to the live edge when necessary, and reconnects without clearing the last good Canvas frame.

The page chooses transports in this order:

1. H.264/MSE when the server reports the encoder healthy and the browser supports the AVC MIME type.
2. Existing latest-JPEG polling when encoder initialization, runtime health, transport, or browser capability fails.

The H.264 branch is display-only. It never changes evidence timestamps, NPU cadence, formal-session state, or recorded-demo ownership.

## Load guard

Encoding runs outside the camera and NPU workers. The encoder records encode duration and consecutive failures. It disables itself for a cooldown and exposes an unhealthy state when initialization fails, repeated encode failures occur, or sustained encode time exceeds the frame interval. The page then falls back to JPEG automatically.

## Manual-control design

The host v3 lease protocol remains authoritative. `precheck_skip_auto_adjust` only stops automatic search; it does not grant manual ownership. A client must then send `gimbal_manual_takeover`, receive the final `push_gimbal_command_result`, store its `lease_token`, and renew it with `gimbal_lease_keepalive` every five seconds.

Android behavior:

- During SEARCHING/CENTERING/FRAMING, Skip is enabled. It sends skip and then takeover.
- In MANUAL_AVAILABLE or HOLDING without a lease, the first nudge/center/stop transparently acquires the lease and then performs the requested action.
- Final asynchronous command results are correlated by session and sequence; queue acceptance is not treated as motor completion.
- Lease token, owner, epoch, and expiry are kept only for the current connection/session and cleared on disconnect, session change, release, or expiry.
- Confirm is allowed for a held/manual-available position even without a lease. If the App owns a lease, it confirms using the matching owner/token before starting the formal phase.

Web behavior mirrors the same state machine. A stale state poll may not erase a newly issued token unless an active lease is authoritatively owned by another client. Manual actions may acquire a lease on demand. Stop remains available by first acquiring ownership when safe.

## Acceptance criteria

- H.264 preview renders at about 15 FPS on the exhibition Chromium browser without unbounded latency growth.
- Evidence overlay continues to use the latest 3-4 FPS inference result and aligns to the displayed frame coordinates.
- Encoder failure switches to latest-JPEG without restarting `focus_app` or interrupting NPU inference.
- Search can be skipped from App and Web.
- Manual nudge and stop work after search ends and after a transparent takeover.
- App renews the lease and no longer waits for a lease that it never requested.
- Confirm-and-start is not blocked by a false manual-lease requirement.
- Existing session, gimbal safety, BLE reliability, and demo tests remain green.

