# Exact Original MJPEG Restore

## Problem

The national page does not currently contain the original preview client. It
contains an H.264/MSE player, a Canvas-based single-JPEG fallback, and a later
dynamic MJPEG override. Those clients share timers and media geometry state and
can leave the page black or frozen after errors and reconnects.

## Approved design

Use commit `96d40cb` as the source of truth for the live-preview presentation
unit. Restore its native `<img id="frame">`, direct multipart
`/api/demo/stream.mjpg` source, one reconnect timer, load/error handlers, and
media-rectangle synchronization as a unit.

Remove all page-side H.264/MSE, Canvas JPEG polling, dynamically inserted MJPEG
elements, and function overrides. Keep the national session, gimbal, evidence,
timeline, App protocol, and board-side preview producer unchanged. The dormant
server-side H.264 endpoint may remain compiled, but the page must not construct
or select it.

## Acceptance

- The served page contains one live media element: `img#frame`.
- No `liveH264`, `MediaSource`, `createImageBitmap`, `frame.jpg` polling, or
  dynamically inserted `live-mjpeg` client remains in the page.
- Live mode opens exactly one `/api/demo/stream.mjpg` request per client
  generation and reconnects only after the image element emits an error or the
  producer reports a real stall.
- Overlay and replay share the original contain-fit media rectangle.
- National gimbal and session controls remain available and unchanged.
