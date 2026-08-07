# OpenH264 runtime dependency

This directory contains the public API headers and the official ARM64 shared
library from Cisco OpenH264 2.6.0. The application loads the library at runtime;
if it is absent or cannot initialize, the demo page automatically uses the
latest-JPEG preview path.

- Upstream: https://github.com/cisco/openh264
- Release: https://github.com/cisco/openh264/releases/tag/v2.6.0
- Runtime filename: `libopenh264.so.8`
- Purpose: display-only 640-pixel-wide, 15 FPS AVC preview

The upstream source license and Cisco binary license apply to these files.

