# App TODO: Reminder Channel Preferences

## Scope

Host-side support is implemented first so the board can run the core flow before App UI changes.
The App should add settings UI and send preference updates later.

## Channel Contract

Stable channel bits:

- `SCREEN = 1`
- `BAND = 2`
- `SOUND = 4`
- `LIGHT = 8` is legacy input compatibility only. The board maps it to
  `SCREEN` and does not return it as an active channel option.

Board defaults:

- `normal_channels_mask = 7` (`SCREEN | BAND | SOUND`)
- `silent_channels_mask = 3` (`SCREEN | BAND`)
- `sound_volume = 0.7`

The board also accepts channel arrays in `update_preferences`:

- `normal_channels`: array of `"screen"`, `"band"`, `"sound"`, `"light"`
- `silent_channels`: array of `"screen"`, `"band"`, `"sound"`, `"light"`
- `sound_volume`: float `0.0..1.0`

`normal` and `silent` only select which reminder channels are used. They do not change reminder severity.
`silent` filters out `SOUND` even if the App sends it, because silent mode cannot use audible reminders.

## App Work

1. Add a settings section for Normal reminder channels.
2. Add a settings section for Silent reminder channels.
3. Load existing values from `get_preferences`.
4. Save changes with `update_preferences`.
5. Keep default values usable when the user never changes this setting.
6. Add a sound volume slider for `sound_volume` (`0.0..1.0`, default `0.7`).

## Current Medium Status

`SCREEN`, `BAND`, and `SOUND` are the current practical reminder media for demo flow.
`SOUND` uses the host ALSA path and the board applies `sound_volume` before playback when ALSA mixer support is available.

## Phone Signal Constraint

The App should refresh `phone_sync` periodically while a distracting app remains active.
If the App is not connected or does not provide phone usage data, the host must continue the session using camera/vision behavior inputs and must not block.
