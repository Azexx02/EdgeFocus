# Current Round Brief

This file is the compact handoff for the current development round before context compaction.

## Scope

Continue in the same repo only:

```text
C:\Users\Aze\Desktop\BoardCode
```

Ignore `D:\Aze\Documents\developMP257`; it is outside this workstream.

## Task 1: Gimbal Integration

Protocol doc:

```text
docs/gimbal_uart_control_protocol(1).md
```

The document text renders with encoding issues, but command names, responses, ranges, and timing are readable.

Key constraints:

- UART4, PC10 TX / PC11 RX
- 3.3 V TTL
- `115200 8N1`, no flow control
- ASCII commands
- newline terminator: `\r\n` or `\n`
- max command length: 63 bytes excluding terminator
- only one outstanding normal command at a time
- emergency stop: send byte `!` without newline
- after `*_START`, wait for `*_OK` or `ERR,...` before next normal command

V1 business commands:

```text
VER
PING
STOP
STAT
ANGLE
FAULT
SET XY <x_deg> <y_deg>
MOVE XY <x_deg> <y_deg> <speed>
DEG X <x_deg>
DEG Y <y_deg>
CENTER X
```

Do not call in product flow:

```text
TRACK
FOCSTAT
HOLD
HELP
SAFE
LIMIT
HOME*
MARK*
CALSAVE/CALLOAD/CALCLEAR
POS*
TEST*
BOOST*
MOTOR*
```

Startup handshake:

```text
VER
PING
STOP
STAT
FAULT
ANGLE
```

Motion allowed only when `STAT` includes:

```text
SAFE=0,XHOME=1,YHOME=1,CAL=1
```

Timeouts:

- query commands: 500 ms
- `DEG Y`: 10 s
- `DEG X`: 30 s
- `CENTER X`: 30 s
- `SET XY`: 45 s
- `MOVE XY`: 60 s

On motion timeout:

1. Send `!`.
2. Wait 200 ms.
3. Send `STOP`.
4. Query `FAULT` and `ANGLE`.
5. Enter gimbal fault/offline state; do not auto-retry the motion.

## Task 2: App Fallback For Board UI/Sound Failure

New self-designed baseboard risk:

- local screen driver may fail
- local sound driver may fail
- local screen flash may fail if display is unavailable

Product requirement:

- Host remains the authority for session, behavior, reminder decisions, reports, history, and feedback.
- App becomes a fallback presentation/output surface if board screen/sound cannot be used.
- Expose all interaction/display/reminder control data through App-facing BLE GATT APIs and push events.

Expose enough to App for:

- session start/current/pause/resume/end
- countdown/progress and session phase
- precheck/gimbal status and manual skip
- camera/vision health
- focus/behavior state
- phone_sync input and resulting behavior state
- reminder event: severity, message/category, allowed channels, actual attempted channels
- App-side sound reminder fallback
- App-side screen flash fallback
- local board display/sound availability
- band availability and alert attempt status where known
- report/history/feedback
- preferences: normal/silent channel masks and `sound_volume`

Reminder semantics to preserve:

- `PHONE_IN_USE` and `PHONE_IN_PLAYING` are behavior inputs only.
- They do not directly force fixed reminder levels.
- Silent mode filters channels only; it does not lower severity.
- Missing phone connection must not block camera/vision session flow.

## Existing Verified Baseline

The main loop was previously verified on the board:

```text
start -> skip_precheck -> focusing -> phone_sync -> reminder -> pause -> resume -> end -> report -> history -> feedback
```

Real hardware logs showed:

- SPI LCD initialized through `/dev/spidev0.0`
- GATT advertisement registered as `EdgeFocus`
- BLE band connected and Alert written
- reminder tone played through `aplay`
- camera opened at index 7, 640x480
- NPU real adapter initialized

Keep `focus_app.service` disabled on the development SD-card system unless explicitly requested.
