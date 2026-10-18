# MJPEG overlay and explicit gimbal lease regression repair

Date: 2026-07-22
Status: Approved

## Scope

This change applies only to the national-competition host application. The
verified onsite source is a reference for the previously correct preview path;
it is not restored as a whole and does not replace national-version features.

## Preview design

- The live exhibition page uses the original continuous multipart MJPEG stream
  directly in an `img` element.
- The existing latest-frame store remains the sole preview source. A slow or
  disconnected browser must not back-pressure camera capture or NPU inference.
- Restore the previously verified media rectangle and SVG geometry as one unit.
  The browser holds the newest real NPU evidence between 3-4 FPS inference
  updates; it does not synthesize, interpolate, or smooth evidence.
- Keep the H.264 encoder and endpoint in the national source tree, but do not
  select H.264 automatically from the exhibition page in this repair.
- Do not use the newer repeated single-JPEG-to-Canvas fallback for normal live
  preview. It is not equivalent to the original MJPEG path.
- Recorded NPU and frontend replay modes keep their existing ownership and
  playback behavior.

## Gimbal control design

- Direction, fine-tune, center, and stop controls are enabled only when this
  browser owns the current manual lease.
- Only the explicit Take Over action may acquire a lease.
- A motion action must never acquire, steal, or retry a lease implicitly.
- Lease mismatch, expiry, session change, or another owner clears the local
  token and requires the user to press Take Over again.
- Release remains available only to the current owner. Recovery/realignment
  remains governed by host state and is not converted into manual ownership.

## Non-goals

- No change to pose inference, subject selection, Presence/Prone/Fusion, or the
  evidence timeline.
- No display-only smoothing to conceal a frame/geometry regression.
- No rollback of national-version session, gimbal, H.264 backend, or protocol
  functionality.

## Verification

- Static source review confirms the page binds the live image to
  `/api/demo/stream.mjpg` and does not auto-select AVC.
- Browser checks cover aspect-ratio resize, evidence alignment, source/session
  changes, and explicit takeover gating.
- Host build and relevant tests must pass before board deployment.
- Board acceptance checks a stationary subject, deliberate motion, browser
  resize, PRECHECK takeover, nudge, release, and a ten-minute preview run.

## Rollback

The change is isolated to the exhibition-page client. The H.264 server backend
remains present and can be re-enabled later after evidence-to-video timestamp
binding is designed and tested independently.
