# EdgeFocusApp release layout

This repository keeps reviewable and releasable files in a small set of
top-level areas:

- `app/src/main/`: Android application source and resources.
- `app/src/test/`: JVM unit tests.
- `app/schemas/`: Room migration schemas required by the app build.
- `docs/`: project notes and implementation documentation.
- Gradle files in the repository root and `app/`: build configuration.

Local agent instructions, temporary plans, build outputs, review zip files,
and backup copies are ignored by git and should not be included in source
review packages.
