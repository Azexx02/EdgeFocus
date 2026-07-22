# PRECHECK one-tap skip and control-state repair

Date: 2026-07-22
Status: Approved as part of the national-version integration repair

## Behavior

- Skip Automatic Adjustment is available while the host is actively searching,
  even when overall preparation has not yet reached READY.
- One tap sends only `precheck_skip_auto_adjust`; it does not acquire a manual
  lease as a hidden side effect.
- The App polls authoritative board state for a bounded interval and applies the
  first non-automatic PRECHECK state so Confirm and manual controls update
  without requiring a second tap.
- Confirm remains independent from manual ownership when the board reports a
  held, manually available, confirmed, degraded, or no-gimbal position.
- A manual direction, center, or stop action may acquire one lease on demand
  using the existing App transaction. Automatic search and tracking remain on.

## Verification

- Existing confirmation-policy and manual-transaction tests continue to pass.
- Build the debug APK and verify Home and Control Panel share the same gating.
