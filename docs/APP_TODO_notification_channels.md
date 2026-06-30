# App TODO: Reminder Channel Preferences

## Scope

Host-side support is implemented first so the board can run the core flow before App UI changes.
The App should add settings UI and send preference updates later.

## Channel Contract

Stable channel bits:

- `SCREEN = 1`
- `BAND = 2`
- `SOUND = 4`
- `LIGHT = 8`

Board defaults:

- `normal_channels_mask = 3` (`SCREEN | BAND`)
- `silent_channels_mask = 2` (`BAND`)

The board also accepts channel arrays in `update_preferences`:

- `normal_channels`: array of `"screen"`, `"band"`, `"sound"`, `"light"`
- `silent_channels`: array of `"screen"`, `"band"`, `"sound"`, `"light"`

`normal` and `silent` only select which reminder channels are used. They do not change reminder severity.

## App Work

1. Add a settings section for Normal reminder channels.
2. Add a settings section for Silent reminder channels.
3. Load existing values from `get_preferences`.
4. Save changes with `update_preferences`.
5. Keep default values usable when the user never changes this setting.

## Current Medium Status

`SCREEN` and `BAND` are the current practical reminder media for demo flow.
`SOUND` and `LIGHT` are reserved in the contract but may not produce visible/audible hardware output until those media are fully connected on the host.

## Phone Signal Constraint

The App should refresh `phone_sync` periodically while a distracting app remains active.
If the App is not connected or does not provide phone usage data, the host must continue the session using camera/vision behavior inputs and must not block.
