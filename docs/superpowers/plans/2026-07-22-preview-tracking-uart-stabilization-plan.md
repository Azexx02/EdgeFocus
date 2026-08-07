# Preview, Tracking, and UART Stabilization Plan

Date: 2026-07-22

1. Make camera, encoder, socket, and browser preview boundaries latest-only and expose real transport age/FPS.
2. Keep immutable 3-4 FPS evidence as an explicit sample-and-hold overlay over the live preview.
3. Replace inference-paced fixed gimbal steps with a 100 ms planner using held intent, hysteresis, proportional slowdown, acceleration limits, and command coalescing.
4. Reduce host probe contention and make G431 UART RX/TX rearming resilient with categorized diagnostics.
5. Add focused tests, build both artifacts, deploy the host, and verify preview age, tracking motion, and UART logs.

Rollback is component-local: retain direct MJPEG image fallback, retain compatibility with the currently flashed protocol-3 firmware, and keep the old tracker implementation recoverable from version control.
