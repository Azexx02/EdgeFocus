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
endif()

# ===========================================================================
# 8. BLE GATT Server Test (BleGattServer API commands)
#    Tests: all 10 API commands, JSON parsing, session lifecycle
#    Deps: SQLiteStore, ReportGenerator, ReminderPolicy, DeviceManager
# ===========================================================================
add_executable(test_ble_gatt
    test_ble_gatt.cpp
    ../network/AppCommandService.cpp
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
    Threads::Threads sqlite3_lib nlohmann_json::nlohmann_json edgefocus_distraction_core)
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
    Threads::Threads sqlite3_lib nlohmann_json::nlohmann_json edgefocus_distraction_core)
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
