# EdgeFocus Demo HTTP Console

Host-side only browser console for board diagnostics and manual control/testing.  
This page is **not** the production App API.

Compile it only when required:

```bash
cmake .. -DEDGEFOCUS_WITH_DEMO_HTTP=ON
./focus_app --demo-http-port 18080
```

Open from:

```text
http://<board-ip>:18080/
```

By default, `EDGEFOCUS_WITH_DEMO_HTTP` is **not enabled** and `focus_app` does not compile or run this page.

## Remove the Demo HTTP Server

Delete these items when building without the feature:

- `focus_app/network/DemoHttpServer.h`
- `focus_app/network/DemoHttpServer.cpp`
- `CMakeLists.txt` `EDGEFOCUS_WITH_DEMO_HTTP` source and compile definition
- `main.cpp` `#ifdef EDGEFOCUS_WITH_DEMO_HTTP` include / registration blocks

## UI Controls

Open `http://<board-ip>:18080/`, then use `/demo/command`.

Supported overrides:

- `force_focus_state`: force visible UI state and text
- `force_phone_state`: force app-side phone state input
- `force_precheck_guidance`: force guidance messages
- `trigger_reminder`: emit `LEVEL_1..3` reminder events for UI validation
- `set_output_route`: set `screen_output_route` / `sound_output_route` (`board` or `phone`)
- `set_demo_counts`: set current counts shown in UI
- `gimbal_nudge`: send `step_deg` (`1..8`), default `3`
- `clear_overrides`: clear manual overrides

## Demo Flow

Recommended test flow:

```text
camera_preview + phone_sync(PHONE_IN_USE/PHONE_IN_PLAYING/PHONE_IN_LEARNING)
-> behavior context updated with vision + phone input
-> ReminderPolicy emits LEVEL_1..3
-> reminder command executed
```

Notes:

- `phone_sync` is input only; if phone input is missing, demo should continue with vision path.
- `PHONE_IN_USE` / `PHONE_IN_PLAYING` / `PHONE_IN_LEARNING` are behavior-layer phone semantics.
- Resulting reminder output is still `LEVEL_1..3` from behavior/policy, then channel filtering by host mode.

## Demo Checklist

1. Run camera stream and check `camera_preview` updates
2. Tune `force_precheck_guidance` if precheck guidance is noisy
3. Enter deterministic demo states with `force_focus_state`
4. Use `force_phone_state` when real app signal is absent
5. Trigger reminders with `trigger_reminder` using fixed levels for UI check
6. Adjust counters with `set_demo_counts`
7. Finish by calling `clear_overrides`
