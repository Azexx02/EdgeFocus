# Per-module test targets
# Each test is a standalone executable with MINIMAL dependencies.
# Deploy and test one at a time on the board.

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
    ../policy/ModeManager.cpp
    ../device/DeviceManager.cpp
    ../device/ScreenController.cpp
    ../device/BleBandController.cpp
    ../device/GpioHelper.cpp
    ../storage/SQLiteStore.cpp
    ../app/AppContext.cpp
    ../app/SystemStateStore.cpp
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
    ../device/ScreenController.cpp
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
# 6. BLE GATT Server Test (BleGattServer API commands)
#    Tests: all 9 API commands, JSON parsing, session lifecycle
#    Deps: SQLiteStore, ReportGenerator, ReminderPolicy, DeviceManager
# ===========================================================================
add_executable(test_ble_gatt
    test_ble_gatt.cpp
    ../network/BleGattServer.cpp
    ../policy/ReminderPolicy.cpp
    ../policy/ModeManager.cpp
    ../device/DeviceManager.cpp
    ../device/ScreenController.cpp
    ../device/BleBandController.cpp
    ../device/GpioHelper.cpp
    ../storage/SQLiteStore.cpp
    ../storage/ReportGenerator.cpp
    ../app/AppContext.cpp
    ../app/SystemStateStore.cpp
    ../app/Logger.cpp)
target_include_directories(test_ble_gatt PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(test_ble_gatt PRIVATE Threads::Threads sqlite3_lib)
add_test(NAME test_ble_gatt COMMAND test_ble_gatt)

# ===========================================================================
# 7. Integration Test (module-to-module data contracts)
#    Tests: FocusState flow, InterventionCommand flow, SessionInfo flow,
#           snapshot fields, ModeManager sync, Report lifecycle
#    Deps: all modules
# ===========================================================================
add_executable(test_integration
    test_integration.cpp
    ../network/BleGattServer.cpp
    ../policy/ReminderPolicy.cpp
    ../policy/ModeManager.cpp
    ../behavior/SlidingWindow.cpp
    ../behavior/EventDetector.cpp
    ../behavior/RiskScorer.cpp
    ../behavior/FocusStateMachine.cpp
    ../behavior/BehaviorEngine.cpp
    ../device/DeviceManager.cpp
    ../device/ScreenController.cpp
    ../device/BleBandController.cpp
    ../device/GpioHelper.cpp
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
    ../app/Logger.cpp)
target_include_directories(test_integration PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(test_integration PRIVATE Threads::Threads sqlite3_lib)
add_test(NAME test_integration COMMAND test_integration)
