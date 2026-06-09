# Test infrastructure for focus_app
# Individual test files will be added by W03/W05/W06 etc.
#
# Example for a future test:
#   add_executable(test_behavior_fsm test_behavior_fsm.cpp)
#   target_include_directories(test_behavior_fsm PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
#   target_link_libraries(test_behavior_fsm PRIVATE Threads::Threads)
#   add_test(NAME test_behavior_fsm COMMAND test_behavior_fsm)

# W03: VisionResult structure tests
add_executable(test_vision_result
    test_vision_result.cpp
    ../vision/VisionEngine.cpp
    ../vision/InferenceScheduler.cpp
    ../vision/FrameQuality.cpp
    ../vision/ModelRegistry.cpp
    ../vision/PoseModelAdapter.cpp
    ../vision/MockVisionSource.cpp
    ../app/AppContext.cpp
    ../app/Logger.cpp)
target_include_directories(test_vision_result PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(test_vision_result PRIVATE Threads::Threads)
add_test(NAME test_vision_result COMMAND test_vision_result)

# W05: Behavior FSM tests
add_executable(test_behavior_fsm 
    test_behavior_fsm.cpp 
    ../behavior/SlidingWindow.cpp 
    ../behavior/EventDetector.cpp 
    ../behavior/RiskScorer.cpp 
    ../behavior/FocusStateMachine.cpp)
target_include_directories(test_behavior_fsm PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(test_behavior_fsm PRIVATE Threads::Threads)
add_test(NAME test_behavior_fsm COMMAND test_behavior_fsm)

# W06: Reminder Policy tests
add_executable(test_reminder_policy
    test_reminder_policy.cpp
    ../policy/ReminderPolicy.cpp
    ../policy/ModeManager.cpp
    ../app/AppContext.cpp
    ../app/SystemStateStore.cpp
    ../storage/SQLiteStore.cpp
    ../app/Logger.cpp)
target_include_directories(test_reminder_policy PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(test_reminder_policy PRIVATE Threads::Threads sqlite3_lib)
add_test(NAME test_reminder_policy COMMAND test_reminder_policy)

# W08: Storage tests
add_executable(test_storage 
    test_storage.cpp 
    ../storage/SQLiteStore.cpp 
    ../storage/ReportGenerator.cpp
    ../app/AppContext.cpp
    ../app/Logger.cpp)
target_include_directories(test_storage PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(test_storage PRIVATE Threads::Threads sqlite3_lib)
add_test(NAME test_storage COMMAND test_storage)

# W07: Device Manager tests
add_executable(test_device_manager
    test_device_manager.cpp
    ../device/DeviceManager.cpp
    ../device/ScreenController.cpp
    ../device/BleBandController.cpp
    ../app/AppContext.cpp
    ../app/SystemStateStore.cpp
    ../app/Logger.cpp)
target_include_directories(test_device_manager PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(test_device_manager PRIVATE Threads::Threads)
add_test(NAME test_device_manager COMMAND test_device_manager)
