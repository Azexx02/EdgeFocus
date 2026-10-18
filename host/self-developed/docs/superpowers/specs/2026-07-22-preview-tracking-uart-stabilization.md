# Preview, Tracking, and UART Stabilization Design

Date: 2026-07-22

## Goal

Improve three connected live-session paths without changing product truth or
masking failures in the display:

1. Render the live camera at approximately 15 FPS with bounded latency.
2. Keep the latest 3-4 FPS inference evidence visible between inference frames.
3. Remove visible step-and-reverse gimbal tracking jitter.
4. Reduce avoidable UART traffic and make isolated G431 receive errors recoverable.

Formal-session ownership, PRECHECK semantics, phone facts, Fusion, and reminder
behavior are unchanged.

## Chosen Architecture

### 1. Low-latency preview

`CameraPipeline` remains the only `VideoCapture` owner. It captures the full-rate
camera stream, publishes approximately every second frame to `PreviewFrameStore`,
and pushes the same capture sequence into the drop-oldest inference queue.

The preview path is latest-only at every boundary:

```text
Camera (about 30 FPS)
  -> PreviewFrameStore (latest raw frame, about 15 FPS)
  -> one JPEG encoder (latest encoded frame)
  -> multipart stream (bounded socket backlog)
  -> browser stream parser (latest complete JPEG only)
  -> rendered canvas (about 15 FPS)
```

Implementation requirements:

- Request a camera backend buffer depth of one when supported. Failure to apply
  the hint is diagnostic only; camera open must still succeed.
- Preserve the current 67 ms encoder cadence and do not create a second camera.
- After encoding, discard an already-stale JPEG when a newer source frame exists
  and the encoded frame age exceeds 150 ms. Do not loop indefinitely while catching up.
- Limit each MJPEG client socket's unsent buffer and use a bounded send deadline.
  A client that cannot consume a complete frame before the deadline reconnects;
  the server never builds an unbounded frame queue.
- Replace the opaque browser `<img src="...mjpg">` consumption path with a
  `fetch()` multipart parser. Keep at most one decoded image pending and render
  only the newest complete JPEG. Preserve the existing `<img>` path as a fallback
  when streaming fetch is unavailable.
- Report client-rendered FPS separately from server encoder FPS. Do not present
  server FPS as browser FPS.

### 2. Evidence reuse and synchronization

Inference remains governed by `VisionCadencePolicy`: approximately 4 FPS in
PRECHECK/Home and 3 FPS in Go. Each `VisualSnapshot` remains immutable evidence
for its original camera frame.

The browser holds the latest `VisualSnapshot` until a newer `frame_id` arrives.
It draws that evidence over each intervening live preview frame. This is an
explicit sample-and-hold contract, not interpolated or fabricated inference.

The state API exposes:

- latest raw preview sequence and capture timestamp;
- latest encoded sequence and capture age;
- server stream FPS;
- latest evidence frame and evidence age;
- browser-rendered FPS in the page only.

This makes preview latency and evidence age independently observable.

### 3. Smooth visual tracking

`GimbalVisualTracker` becomes a stateful visual target estimator and bounded
motion planner. Visual inference updates the desired direction; a periodic tick
emits motion commands independently of the 3-4 FPS inference cadence.

Initial production parameters:

- control period: 100 ms;
- visual EMA alpha: 0.25;
- inner deadband: retain approximately x=0.06, y=0.07;
- outer/re-entry band: x=0.09, y=0.10;
- reversal confirmation: two fresh inference frames;
- maximum yaw velocity: 12 degrees/second;
- maximum pitch velocity: 10 degrees/second;
- maximum acceleration: 24 degrees/second squared;
- minimum transmitted target delta: 0.35 degrees;
- held visual intent: up to 450 ms;
- controlled decay to zero: complete by 700 ms;
- target-lost HOLD: retain the existing 1500 ms safety boundary.

Behavior:

- Far targets produce a bounded continuous velocity, not repeated fixed 1.5-degree steps.
- Near the target, velocity falls proportionally.
- Entering the inner deadband decelerates to zero and HOLDs.
- Leaving the inner band does not restart movement until the outer band is crossed.
- Direction reversal requires two consecutive fresh visual observations in the
  new direction. Until confirmed, velocity decelerates instead of reversing.
- Duplicate inference frames do not alter intent.
- PAUSED, manual takeover, release, fault, and session stop reset the planner and HOLD.
- A stale visual target is never extrapolated indefinitely.

No face model is added. The existing nose/eyes/subject-box fallback remains the
tracking point source.

### 4. UART reliability

The host remains the sole UART4 owner and the board remains compatible with
`uart_only` mode.

Host changes:

- Suppress unchanged tracking targets.
- Schedule health probes away from an in-flight command.
- Use a 1 second probe period while armed and holding, and a 2 second period while
  actively tracking. Command timeout/fault evidence still triggers immediate handling.
- Continue to treat three consecutive health timeouts as link degradation.
- Log UART error deltas by category when firmware provides them; never convert a
  rising counter into a healthy status.

G431 changes:

- Preserve protocol negotiation after an isolated recoverable FE/NE/ORE/PE error.
- Drop the partial input line, clear the hardware flags, and re-arm receive.
- Check every receive re-arm result. If immediate re-arm fails, retry from the
  non-interrupt poll loop with a bounded diagnostic counter.
- Start the next queued TX block from the TX-complete callback.
- Expose separate overrun, framing, noise, parity, RX re-arm, RX drop, and TX drop counters.
- Only repeated command timeouts or an asserted device fault cause host HOLD,
  re-negotiation, or degradation.

This software work improves tolerance and observability. Persistent FE/NE errors
still require electrical inspection (grounding, routing, connector, optional 47 ohm
series resistance); the software must not relabel physical noise as fixed.

## Failure Behavior

- Preview failure affects only the exhibition page; the formal camera/inference
  session continues.
- A slow browser reconnects to the latest preview and does not block control APIs.
- Evidence remains visibly aged when inference is slow; it is not synthesized.
- Tracking planner failure or stale evidence ends in HOLD, not free-running motion.
- UART link loss disables tracking and PRECHECK motion through the existing optional
  feature degradation path; Presence/Prone/Phone Fusion continues.

## Verification

### Automated

- Preview store/encoder tests: sequence monotonicity, latest-frame replacement,
  stale-encode discard, source switch, and shutdown.
- Tracking tests: stationary noisy target produces no commands; far target ramps;
  near target decelerates; one-frame reversal is ignored; two-frame reversal is
  accepted; stale intent decays and HOLDs; pause/reset clears state.
- Host UART tests: probe cadence by motion state, command/probe exclusion, unchanged
  target coalescing, error counter parsing, and three-timeout degradation.
- G431 tests: isolated UART error preserves negotiation, partial line is discarded,
  failed re-arm retries, counters are categorized, and TX queue continues after completion.

### Board acceptance

- Live preview rendered median >=12 FPS with a 15 FPS target.
- Parsed stream capture age p95 <=250 ms on the exhibition LAN.
- Evidence remains at the real 3-4 FPS and is held between results.
- No steadily growing browser/video delay during a 10-minute session.
- A stationary face does not cause visible direction reversals or repeated motor kicks.
- A deliberate move across the frame recenters smoothly within the accepted extra
  0.5-1 second response time.
- No `INVALID_STATE`, control-lease oscillation, or preview thread blockage.
- UART error categories and rates are recorded before and after; no RX/TX drops and
  no protocol renegotiation from a single recoverable line error.

## Rollback

- Preview fallback: retain the existing direct MJPEG `<img>` client path.
- Tracking fallback: configuration can disable periodic planning and restore the
  previous inference-driven updates without changing manual control.
- Firmware and host releases are versioned independently; deploy host only after
  it remains compatible with the currently flashed protocol-3 firmware.
