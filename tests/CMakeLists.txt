# Per-module test targets
# Each test is a standalone executable with MINIMAL dependencies.
# Deploy and test one at a time on the board.

# ===========================================================================
# 0. Session Runtime Test (new session-centered lifecycle)
#    Tests: prepare rollback, optional degradation, phase transitions,
#           idempotency, and cross-session snapshot isolation.
# ===========================================================================
add_executable(test_session_runtime
    test_session_runtime.cpp
    ../session/SessionSnapshotPublisher.cpp
    ../session/SessionRuntime.cpp
    ../session/SessionOrchestrator.cpp)
target_include_directories(test_session_runtime PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(test_session_runtime PRIVATE Threads::Threads)
add_test(NAME test_session_runtime COMMAND test_session_runtime)

add_executable(test_precheck_formal_boundary
    test_precheck_formal_boundary.cpp
    ../session/SessionSnapshotPublisher.cpp
    ../session/SessionRuntime.cpp
    ../session/SessionOrchestrator.cpp
    ../session/LegacySessionProjectionFeature.cpp
    ../app/AppContext.cpp
    ../app/SystemStateStore.cpp
    ../app/SessionController.cpp
    ../app/Logger.cpp
    ../storage/SQLiteStore.cpp
    ../storage/ReportGenerator.cpp)
target_include_directories(test_precheck_formal_boundary PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(test_precheck_formal_boundary PRIVATE Threads::Threads sqlite3_lib)
add_test(NAME test_precheck_formal_boundary COMMAND test_precheck_formal_boundary)

# ===========================================================================
# 0b. Validated Distraction Core Tests
#     Exact pure-logic tests migrated with the standalone C++ core.
# ===========================================================================
add_executable(test_distraction_vision test_distraction_vision.cpp)
target_link_libraries(test_distraction_vision PRIVATE edgefocus_distraction_core)
add_test(NAME test_distraction_vision COMMAND test_distraction_vision)

add_executable(test_distraction_temporal test_distraction_temporal.cpp)
target_link_libraries(test_distraction_temporal PRIVATE edgefocus_distraction_core)
add_test(NAME test_distraction_temporal COMMAND test_distraction_temporal)

add_executable(test_distraction_fusion test_distraction_fusion.cpp)
target_link_libraries(test_distraction_fusion PRIVATE edgefocus_distraction_core)
add_test(NAME test_distraction_fusion COMMAND test_distraction_fusion)

add_executable(test_distraction_adapter
    test_distraction_adapter.cpp
    ../behavior/PhoneFactBuffer.cpp
    ../behavior/DistractionAssessmentEngine.cpp)
target_include_directories(test_distraction_adapter PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(test_distraction_adapter PRIVATE edgefocus_distraction_core Threads::Threads)
add_test(NAME test_distraction_adapter COMMAND test_distraction_adapter)

# ===========================================================================
# 0c. M33 Fan Control Core Tests
#     Pure logic/protocol tests; no Linux, HAL, OpenAMP, or product runtime.
# ===========================================================================
add_executable(test_fan_control
    ../platform/fan/tests/test_fan_control.cpp)
target_link_libraries(test_fan_control PRIVATE edgefocus_fan_core)
add_test(NAME test_fan_control COMMAND test_fan_control)

add_executable(test_fan_protocol
    ../platform/fan/tests/test_fan_protocol.cpp)
target_link_libraries(test_fan_protocol PRIVATE edgefocus_fan_core)
add_test(NAME test_fan_protocol COMMAND test_fan_protocol)

add_executable(test_m33_fan_controller
    ../platform/fan/tests/test_m33_fan_controller.cpp)
target_link_libraries(test_m33_fan_controller PRIVATE edgefocus_m33_fan_app)
add_test(NAME test_m33_fan_controller COMMAND test_m33_fan_controller)

if(UNIX AND NOT APPLE)
    add_executable(test_fan_bridge
        ../platform/fan/tests/test_fan_bridge.cpp
        ../platform/fan/bridge/FrameStreamDecoder.cpp
        ../platform/fan/bridge/ThermalSource.cpp)
    target_include_directories(test_fan_bridge PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../platform/fan/bridge)
    target_link_libraries(test_fan_bridge PRIVATE edgefocus_fan_core)
    add_test(NAME test_fan_bridge COMMAND test_fan_bridge)
endif()

# ===========================================================================
# 1. Storage Layer Test (SQLiteStore + ReportGenerator)
#    Tests: logEvent, queryEvents, generateReport, extended fields
#    Deps: SQLite3 only
# ===========================================================================
add_executable(test_storage
    test_storage.cpp
    ../storage/SQLiteStore.cpp
    ../storage/ReportGenerator.cpp
    ../app/AppContext.cpp
    ../app/Logger.cpp)
target_include_directories(test_storage PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(test_storage PRIVATE Threads::Threads sqlite3_lib)
add_test(NAME test_storage COMMAND test_storage)

# ===========================================================================
# 2. Behavior FSM Test (FocusStateMachine + EventDetector + RiskScorer)
#    Tests: focus, head_down, phone, away, looking_away, calibration gate
#    Deps: none (pure logic, no I/O)
# ===========================================================================
add_executable(test_behavior_fsm
    test_behavior_fsm.cpp
    ../behavior/SlidingWindow.cpp
    ../behavior/EventDetector.cpp
    ../behavior/RiskScorer.cpp
    ../behavior/FocusStateMachine.cpp)
target_include_directories(test_behavior_fsm PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(test_behavior_fsm PRIVATE Threads::Threads)
add_test(NAME test_behavior_fsm COMMAND test_behavior_fsm)

# ===========================================================================
# 3. Reminder Policy Test (ReminderPolicy + ModeManager)
#    Tests: escalation, fatigue, silent mode, ack, phone levels
#    Deps: SQLiteStore (for event logging), DeviceManager (for cmd dispatch)
# ===========================================================================
add_executable(test_reminder_policy
    test_reminder_policy.cpp
    ../policy/ReminderPolicy.cpp
    ../policy/NotificationRouter.cpp
    ../policy/ModeManager.cpp
    ../device/DeviceManager.cpp
    ../device/GimbalMonitor.cpp
    ../device/GimbalController.cpp
    ../device/ScreenController.cpp
    ../device/SoundController.cpp
    ../device/BleBandController.cpp
    ../device/GpioHelper.cpp
    ../storage/SQLiteStore.cpp
    ../app/AppContext.cpp
    ../app/SystemStateStore.cpp
    ../app/SessionController.cpp
    ../app/Logger.cpp)
target_include_directories(test_reminder_policy PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(test_reminder_policy PRIVATE Threads::Threads sqlite3_lib)
add_test(NAME test_reminder_policy COMMAND test_reminder_policy)

# ===========================================================================
# 4. Device Manager Test (DeviceManager + ScreenController + BleBand)
#    Tests: command queue, screen flash, BLE alert
#    Deps: none (hardware test, Linux only)
# ===========================================================================
add_executable(test_device_manager
    test_device_manager.cpp
    ../device/DeviceManager.cpp
    ../policy/NotificationRouter.cpp
    ../device/ScreenController.cpp
    ../device/SoundController.cpp
    ../device/BleBandController.cpp
    ../device/GpioHelper.cpp
    ../app/AppContext.cpp
    ../app/SystemStateStore.cpp
    ../app/Logger.cpp)
target_include_directories(test_device_manager PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(test_device_manager PRIVATE Threads::Threads)
add_test(NAME test_device_manager COMMAND test_device_manager)

# ===========================================================================
# 5. Vision Result Test (VisionResult + MockVisionSource + InferenceScheduler)
#    Tests: ConfidenceValue, VisionResult helpers, mock scenarios, CPU fallback
#    Deps: none (pure logic + mock data)
# ===========================================================================
add_executable(test_vision_result
    test_vision_result.cpp
    ../camera/CameraPipeline.cpp
    ../camera/PreviewFrameStore.cpp
    ../session/SessionSnapshotPublisher.cpp
    ../vision/FrameQuality.cpp
    ../vision/ModelRegistry.cpp
    ../vision/PoseModelAdapter.cpp
    ../vision/MockVisionSource.cpp
    ../vision/InferenceScheduler.cpp
    ../vision/VisionEngine.cpp
    ../app/AppContext.cpp
    ../app/SystemStateStore.cpp
    ../app/Logger.cpp)
target_include_directories(test_vision_result PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(test_vision_result PRIVATE Threads::Threads)
add_test(NAME test_vision_result COMMAND test_vision_result)

# ===========================================================================
# 6. Picture Vision Smoke (image -> frame -> InferenceScheduler fallback -> VisionResult)
#    Tests: picture_test directory loading path fallback, frame build, and FALLBACK output sanity.
#    If OpenCV is not enabled, this test returns SKIP status via exit code 0.
# ===========================================================================
add_executable(test_picture_vision
    test_picture_vision.cpp
    ../vision/FrameQuality.cpp
    ../vision/ModelRegistry.cpp
    ../vision/PoseModelAdapter.cpp
    ../vision/InferenceScheduler.cpp
    ../behavior/SlidingWindow.cpp
    ../behavior/EventDetector.cpp
    ../behavior/RiskScorer.cpp
    ../behavior/FocusStateMachine.cpp
    ../behavior/BehaviorEngine.cpp
    ../storage/SQLiteStore.cpp
    ../app/AppContext.cpp
    ../app/SystemStateStore.cpp
    ../app/SessionController.cpp
    ../app/Logger.cpp)
target_include_directories(test_picture_vision PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(test_picture_vision PRIVATE Threads::Threads sqlite3_lib nlohmann_json::nlohmann_json)
if(EDGEFOCUS_WITH_OPENCV)
    target_compile_definitions(test_picture_vision PRIVATE EDGEFOCUS_WITH_OPENCV)
    target_include_directories(test_picture_vision PRIVATE ${OpenCV_INCLUDE_DIRS})
    target_link_libraries(test_picture_vision PRIVATE ${OpenCV_LIBS})
endif()
add_test(NAME test_picture_vision COMMAND test_picture_vision)

# ===========================================================================
# 7. Notification Router Test (intent + preference routing)
#    Tests: NORMAL/SILENT/level/NONE routing behavior.
# ===========================================================================
add_executable(test_notification_router
    test_notification_router.cpp
    ../policy/NotificationRouter.cpp)
target_include_directories(test_notification_router PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(test_notification_router PRIVATE Threads::Threads)
add_test(NAME test_notification_router COMMAND test_notification_router)

add_executable(test_app_event_outbox
    test_app_event_outbox.cpp
    ../network/AppEventOutbox.cpp
    ../policy/NotificationRouter.cpp)
target_include_directories(test_app_event_outbox PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(test_app_event_outbox PRIVATE Threads::Threads nlohmann_json::nlohmann_json)
add_test(NAME test_app_event_outbox COMMAND test_app_event_outbox)

# ===========================================================================
# 8. Demo HTTP Parser Test (video-only helper parsing)
#    Tests: ASCII demo page request parsing, Content-Length buffering, bad headers.
#    Deps: none (header-only parser helper).
# ===========================================================================
if(EDGEFOCUS_WITH_DEMO_HTTP)
    add_executable(test_demo_http
        test_demo_http.cpp)
    target_include_directories(test_demo_http PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
    target_link_libraries(test_demo_http PRIVATE Threads::Threads nlohmann_json::nlohmann_json)
    add_test(NAME test_demo_http COMMAND test_demo_http)

    add_executable(test_h264_mp4
        test_h264_mp4.cpp
        ../network/FragmentedMp4Muxer.cpp)
    target_include_directories(test_h264_mp4 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
    add_test(NAME test_h264_mp4 COMMAND test_h264_mp4)
endif()

# ===========================================================================
# 8. BLE GATT Server Test (BleGattServer API commands)
#    Tests: all 10 API commands, JSON parsing, session lifecycle
#    Deps: SQLiteStore, ReportGenerator, ReminderPolicy, DeviceManager
# ===========================================================================
add_executable(test_ble_gatt
    test_ble_gatt.cpp
    ../network/AppCommandService.cpp
    ../network/AppEventOutbox.cpp
    ../network/BleGattServer.cpp
    ../session/SessionSnapshotPublisher.cpp
    ../session/SessionRuntime.cpp
    ../session/SessionOrchestrator.cpp
    ../behavior/PhoneFactBuffer.cpp
    ../policy/ReminderPolicy.cpp
    ../policy/NotificationRouter.cpp
    ../policy/ModeManager.cpp
    ../device/DeviceManager.cpp
    ../device/GimbalMonitor.cpp
    ../device/GimbalController.cpp
    ../device/ScreenController.cpp
    ../device/SoundController.cpp
    ../device/BleBandController.cpp
    ../device/GpioHelper.cpp
    ../ui/StatusPage.cpp
    ../ui/UIManager.cpp
    ../ui/StatsPage.cpp
    ../ui/ReviewPage.cpp
    ../ui/SettingsPage.cpp
    ../ui/FrameBuffer.cpp
    ../storage/SQLiteStore.cpp
    ../storage/ReportGenerator.cpp
    ../app/AppContext.cpp
    ../app/SystemStateStore.cpp
    ../app/SessionController.cpp
    ../app/Logger.cpp)
target_include_directories(test_ble_gatt PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(test_ble_gatt PRIVATE
    Threads::Threads sqlite3_lib nlohmann_json::nlohmann_json
    edgefocus_distraction_core edgefocus_power_core edgefocus_gimbal_session_core)
add_test(NAME test_ble_gatt COMMAND test_ble_gatt)

# ===========================================================================
# 9. Integration Test (module-to-module data contracts)
#    Tests: FocusState flow, InterventionCommand flow, SessionInfo flow,
#           snapshot fields, ModeManager sync, Report lifecycle
#    Deps: all modules
# ===========================================================================
add_executable(test_integration
    test_integration.cpp
    ../network/AppCommandService.cpp
    ../network/AppEventOutbox.cpp
    ../network/BleGattServer.cpp
    ../session/SessionSnapshotPublisher.cpp
    ../session/SessionRuntime.cpp
    ../session/SessionOrchestrator.cpp
    ../behavior/PhoneFactBuffer.cpp
    ../policy/ReminderPolicy.cpp
    ../policy/NotificationRouter.cpp
    ../policy/ModeManager.cpp
    ../behavior/SlidingWindow.cpp
    ../behavior/EventDetector.cpp
    ../behavior/RiskScorer.cpp
    ../behavior/FocusStateMachine.cpp
    ../behavior/BehaviorEngine.cpp
    ../device/DeviceManager.cpp
    ../device/GimbalMonitor.cpp
    ../device/GimbalController.cpp
    ../device/ScreenController.cpp
    ../device/SoundController.cpp
    ../device/BleBandController.cpp
    ../device/GpioHelper.cpp
    ../ui/StatusPage.cpp
    ../ui/UIManager.cpp
    ../ui/StatsPage.cpp
    ../ui/ReviewPage.cpp
    ../ui/SettingsPage.cpp
    ../ui/FrameBuffer.cpp
    ../storage/SQLiteStore.cpp
    ../storage/ReportGenerator.cpp
    ../vision/FrameQuality.cpp
    ../vision/ModelRegistry.cpp
    ../vision/PoseModelAdapter.cpp
    ../vision/MockVisionSource.cpp
    ../vision/InferenceScheduler.cpp
    ../vision/VisionEngine.cpp
    ../app/AppContext.cpp
    ../app/SystemStateStore.cpp
    ../app/SessionController.cpp
    ../app/Logger.cpp)
target_include_directories(test_integration PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(test_integration PRIVATE
    Threads::Threads sqlite3_lib nlohmann_json::nlohmann_json
    edgefocus_distraction_core edgefocus_power_core edgefocus_gimbal_session_core)
add_test(NAME test_integration COMMAND test_integration)

if(EDGEFOCUS_WITH_OPENCV AND EDGEFOCUS_WITH_STAI_MPU)
    # Board-only smoke for the real STAI/NPU adapter. It is intentionally not
    # part of the default host build because the SDK and model are board-side.
    find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs)
    find_path(STAI_MPU_INCLUDE_DIR stai_mpu_network.h
        PATHS
            ${CMAKE_SYSROOT}/usr/include/stai_mpu
            /usr/include/stai_mpu
            /usr/local/include/stai_mpu)
    if(NOT STAI_MPU_INCLUDE_DIR)
        message(FATAL_ERROR "EDGEFOCUS_WITH_STAI_MPU=ON but stai_mpu_network.h was not found")
    endif()
    add_executable(test_stai_npu
        test_stai_npu.cpp
        ../vision/PoseModelAdapter.cpp
        ../vision/FrameQuality.cpp)
    target_compile_definitions(test_stai_npu PRIVATE EDGEFOCUS_WITH_OPENCV EDGEFOCUS_WITH_STAI_MPU)
    target_include_directories(test_stai_npu PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${OpenCV_INCLUDE_DIRS}
        ${STAI_MPU_INCLUDE_DIR})
    target_link_libraries(test_stai_npu PRIVATE Threads::Threads ${OpenCV_LIBS} stai_mpu)
endif()

# ===========================================================================
# 10. Gimbal Controller Test (UART parser + state commands)
#     Tests: STAT/ANGLE parsing, error response handling, allowed command set.
#     Deps: none.
# ===========================================================================
add_executable(test_gimbal_controller
    test_gimbal_controller.cpp
    ../device/GimbalController.cpp
    ../app/Logger.cpp)
target_include_directories(test_gimbal_controller PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(test_gimbal_controller PRIVATE Threads::Threads)
add_test(NAME test_gimbal_controller COMMAND test_gimbal_controller)

add_executable(test_gimbal_protocol
    ../platform/gimbal/tests/test_gimbal_protocol.cpp)
target_link_libraries(test_gimbal_protocol PRIVATE edgefocus_gimbal_core)
add_test(NAME test_gimbal_protocol COMMAND test_gimbal_protocol)

add_executable(test_gimbal_state_machine
    ../platform/gimbal/tests/test_gimbal_state_machine.cpp)
target_link_libraries(test_gimbal_state_machine PRIVATE edgefocus_gimbal_core)
add_test(NAME test_gimbal_state_machine COMMAND test_gimbal_state_machine)

add_executable(test_gimbal_motion_policy
    ../platform/gimbal/tests/test_gimbal_motion_policy.cpp)
target_include_directories(test_gimbal_motion_policy PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
add_test(NAME test_gimbal_motion_policy COMMAND test_gimbal_motion_policy)

add_executable(test_gimbal_hardware_service
    ../platform/gimbal/tests/test_gimbal_hardware_service.cpp
    ../app/AppContext.cpp)
target_include_directories(test_gimbal_hardware_service PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(test_gimbal_hardware_service PRIVATE
    edgefocus_gimbal_service Threads::Threads)
add_test(NAME test_gimbal_hardware_service COMMAND test_gimbal_hardware_service)

add_executable(test_gimbal_oneway_stop_diagnostic
    ../platform/gimbal/tests/test_gimbal_oneway_stop_diagnostic.cpp)
target_link_libraries(test_gimbal_oneway_stop_diagnostic PRIVATE
    edgefocus_gimbal_oneway_diagnostic)
add_test(NAME test_gimbal_oneway_stop_diagnostic
    COMMAND test_gimbal_oneway_stop_diagnostic)

add_executable(test_stusb4500_status
    ../platform/power/tests/test_stusb4500_status.cpp)
target_link_libraries(test_stusb4500_status PRIVATE edgefocus_power_core)
add_test(NAME test_stusb4500_status COMMAND test_stusb4500_status)

add_executable(test_workload_profile_policy
    ../platform/power/tests/test_workload_profile_policy.cpp)
target_link_libraries(test_workload_profile_policy PRIVATE edgefocus_power_core)
add_test(NAME test_workload_profile_policy COMMAND test_workload_profile_policy)

add_executable(test_power_source_service
    ../platform/power/tests/test_power_source_service.cpp
    ../app/AppContext.cpp)
target_include_directories(test_power_source_service PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(test_power_source_service PRIVATE
    edgefocus_power_service Threads::Threads)
add_test(NAME test_power_source_service COMMAND test_power_source_service)

add_executable(test_vision_cadence_policy test_vision_cadence_policy.cpp)
target_include_directories(test_vision_cadence_policy PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
add_test(NAME test_vision_cadence_policy COMMAND test_vision_cadence_policy)

add_executable(test_gimbal_precheck test_gimbal_precheck.cpp)
target_include_directories(test_gimbal_precheck PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_definitions(test_gimbal_precheck PRIVATE
    EDGEFOCUS_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(test_gimbal_precheck PRIVATE edgefocus_gimbal_session_core)
add_test(NAME test_gimbal_precheck COMMAND test_gimbal_precheck)

add_executable(test_gimbal_tracking test_gimbal_tracking.cpp)
target_include_directories(test_gimbal_tracking PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(test_gimbal_tracking PRIVATE edgefocus_gimbal_session_core)
add_test(NAME test_gimbal_tracking COMMAND test_gimbal_tracking)
