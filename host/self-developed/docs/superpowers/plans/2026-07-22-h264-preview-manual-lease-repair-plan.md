# H.264 preview and manual lease repair implementation plan

1. Add optional OpenH264 dependency discovery and a bounded preview encoder/muxer.
2. Add H.264 stream/status HTTP endpoints and MSE playback with latest-JPEG fallback.
3. Add unit coverage for MP4 fragments, encoder health fallback, and HTTP routing.
4. Extend Android command models/codecs with owner and lease fields.
5. Correlate `push_gimbal_command_result`, retain the active in-memory lease, and run keepalive.
6. Update PRECHECK transaction and UI policies for skip/takeover/manual-ready/confirm behavior.
7. Run host and Android tests, cross-compile, deploy, and verify browser/BLE flows.

