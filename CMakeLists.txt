cmake_minimum_required(VERSION 3.14)
project(focus_app LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/utf-8 /EHsc)
endif()

option(EDGEFOCUS_WITH_OPENCV "Build camera capture and CPU/STAI preprocessing with OpenCV" OFF)
option(EDGEFOCUS_WITH_STAI_MPU "Build STAI_MPU pose model adapter" OFF)
option(EDGEFOCUS_WITH_DEMO_HTTP "Build optional HTTP demo console" OFF)
option(EDGEFOCUS_WITH_FAN_BRIDGE "Build the standalone A35-to-M33 fan bridge" ON)

add_library(edgefocus_power_core STATIC
    platform/power/Stusb4500Status.cpp
    platform/power/WorkloadProfilePolicy.cpp)
target_include_directories(edgefocus_power_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
if(MSVC)
    target_compile_options(edgefocus_power_core PRIVATE /W4)
else()
    target_compile_options(edgefocus_power_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(edgefocus_power_service STATIC
    platform/power/LinuxI2cPowerSourceIo.cpp
    platform/power/PowerSourceService.cpp
    app/Logger.cpp)
target_include_directories(edgefocus_power_service PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(edgefocus_power_service PUBLIC edgefocus_power_core)
if(MSVC)
    target_compile_options(edgefocus_power_service PRIVATE /W4)
else()
    target_compile_options(edgefocus_power_service PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(edgefocus_gimbal_core STATIC
    platform/gimbal/GimbalProtocol.cpp
    platform/gimbal/GimbalStateMachine.cpp)
target_include_directories(edgefocus_gimbal_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
if(MSVC)
    target_compile_options(edgefocus_gimbal_core PRIVATE /W4)
else()
    target_compile_options(edgefocus_gimbal_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(edgefocus_gimbal_service STATIC
    platform/gimbal/GimbalHardwareService.cpp
    platform/gimbal/LinuxGimbalIo.cpp
    device/GpioHelper.cpp
    app/Logger.cpp)
target_include_directories(edgefocus_gimbal_service PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(edgefocus_gimbal_service PUBLIC edgefocus_gimbal_core)
if(MSVC)
    target_compile_options(edgefocus_gimbal_service PRIVATE /W4)
else()
    target_compile_options(edgefocus_gimbal_service PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(edgefocus_gimbal_session_core STATIC
    device/GimbalPrecheckController.cpp
    device/GimbalVisualTracker.cpp
    device/GimbalVisualEvidence.cpp
    device/GimbalBoardConfig.cpp
    app/Logger.cpp)
target_include_directories(edgefocus_gimbal_session_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(edgefocus_gimbal_session_core PUBLIC
    edgefocus_gimbal_core nlohmann_json::nlohmann_json)
if(MSVC)
    target_compile_options(edgefocus_gimbal_session_core PRIVATE /W4)
else()
    target_compile_options(edgefocus_gimbal_session_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(FetchContent)
FetchContent_Declare(sqlite3 URL https://www.sqlite.org/2024/sqlite-amalgamation-3450100.zip DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(sqlite3)
if(NOT sqlite3_POPULATED)
  FetchContent_Populate(sqlite3)
endif()

# Create a static library for sqlite3
add_library(sqlite3_lib STATIC ${sqlite3_SOURCE_DIR}/sqlite3.c)
target_include_directories(sqlite3_lib PUBLIC ${sqlite3_SOURCE_DIR})

FetchContent_Declare(json URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(json)

add_library(edgefocus_gimbal_oneway_diagnostic STATIC
    tools/GimbalOnewayStopDiagnostic.cpp)
target_include_directories(edgefocus_gimbal_oneway_diagnostic PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR})
if(MSVC)
    target_compile_options(edgefocus_gimbal_oneway_diagnostic PRIVATE /W4)
else()
    target_compile_options(edgefocus_gimbal_oneway_diagnostic PRIVATE
        -Wall -Wextra -Wpedantic)
endif()

add_executable(gimbal_bringup tools/gimbal_bringup.cpp)
target_include_directories(gimbal_bringup PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(gimbal_bringup PRIVATE
    edgefocus_gimbal_service edgefocus_gimbal_oneway_diagnostic
    nlohmann_json::nlohmann_json)

# Validated Presence/Prone/Fusion core migrated from the standalone board demo.
# It remains a separate static library so host logic tests do not pull in the
# camera, session, BLE, UI, or legacy behavior layers.
add_library(edgefocus_distraction_core STATIC
    core/distraction/src/vision.cpp
    core/distraction/src/temporal.cpp
    core/distraction/src/fusion.cpp
    core/distraction/src/portable_model_json.cpp)
target_include_directories(edgefocus_distraction_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/core/distraction/include)
target_link_libraries(edgefocus_distraction_core PUBLIC nlohmann_json::nlohmann_json)
if(MSVC)
    target_compile_options(edgefocus_distraction_core PRIVATE /W4)
else()
    target_compile_options(edgefocus_distraction_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Platform-level fan control logic shared by the Linux bridge and M33 firmware.
# This target is intentionally freestanding from focus_app/session/demo code.
add_library(edgefocus_fan_core STATIC
    platform/fan/core/FanControlCore.cpp
    platform/fan/core/FanProtocol.cpp)
target_include_directories(edgefocus_fan_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/platform/fan/core)
if(MSVC)
    target_compile_options(edgefocus_fan_core PRIVATE /W4)
else()
    target_compile_options(edgefocus_fan_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(edgefocus_m33_fan_app STATIC
    firmware/m33/fan_controller/FanControllerApp.cpp)
target_include_directories(edgefocus_m33_fan_app PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/firmware/m33/fan_controller)
target_link_libraries(edgefocus_m33_fan_app PUBLIC edgefocus_fan_core)
if(MSVC)
    target_compile_options(edgefocus_m33_fan_app PRIVATE /W4)
else()
    target_compile_options(edgefocus_m33_fan_app PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(EDGEFOCUS_WITH_FAN_BRIDGE AND UNIX AND NOT APPLE)
    add_executable(edgefocus-fan-bridge
        platform/fan/bridge/FanBridge.cpp
        platform/fan/bridge/FrameStreamDecoder.cpp
        platform/fan/bridge/RpmsgTransport.cpp
        platform/fan/bridge/ThermalSource.cpp)
    target_include_directories(edgefocus-fan-bridge PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/platform/fan/bridge)
    target_link_libraries(edgefocus-fan-bridge PRIVATE edgefocus_fan_core)
    target_compile_options(edgefocus-fan-bridge PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Production sources. Legacy VisionEngine/BehaviorEngine and board UI are kept
# in the repository for migration tests only and are not linked into focus_app.
set(APP_SOURCES
    main.cpp
    app/Logger.cpp
    app/AppContext.cpp
    app/SystemStateStore.cpp
    app/SessionController.cpp
    session/SessionSnapshotPublisher.cpp
    session/SessionRuntime.cpp
    session/SessionOrchestrator.cpp
    session/LegacySessionProjectionFeature.cpp
    camera/CameraPipeline.cpp
    camera/PreviewFrameStore.cpp
    vision/DistractionPipelineFeature.cpp
    behavior/PhoneFactBuffer.cpp
    behavior/DistractionAssessmentEngine.cpp
    policy/ReminderPolicy.cpp
    policy/NotificationRouter.cpp
    policy/ModeManager.cpp
    device/GpioHelper.cpp
    device/DeviceManager.cpp
    device/GimbalController.cpp
    device/GimbalMonitor.cpp
    device/GimbalSessionFeature.cpp
    device/ScreenController.cpp
    device/SoundController.cpp
    device/BleBandController.cpp
    storage/SQLiteStore.cpp
    storage/ReportGenerator.cpp
    network/AppCommandService.cpp
    network/AppEventOutbox.cpp
    network/BleGattServer.cpp
)

add_executable(focus_app ${APP_SOURCES})
target_include_directories(focus_app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(focus_app PRIVATE
    sqlite3_lib
    nlohmann_json::nlohmann_json
    edgefocus_distraction_core
    edgefocus_power_service
    edgefocus_gimbal_session_core
    edgefocus_gimbal_service)

# Video-only demo console. Default builds exclude the HTTP entry and its
# SystemStateStore/BehaviorEngine override path through EDGEFOCUS_WITH_DEMO_HTTP.
if(EDGEFOCUS_WITH_DEMO_HTTP)
    target_sources(focus_app PRIVATE
        demo/BoardDemoRuntime.cpp
        network/DemoHttpServer.cpp)
    target_compile_definitions(focus_app PRIVATE EDGEFOCUS_WITH_DEMO_HTTP)
endif()

if(EDGEFOCUS_WITH_OPENCV)
    find_package(OpenCV REQUIRED COMPONENTS core imgproc videoio imgcodecs)
    target_compile_definitions(focus_app PRIVATE EDGEFOCUS_WITH_OPENCV)
    target_include_directories(focus_app PRIVATE ${OpenCV_INCLUDE_DIRS})
    target_link_libraries(focus_app PRIVATE ${OpenCV_LIBS})
endif()

if(EDGEFOCUS_WITH_STAI_MPU)
    find_path(STAI_MPU_INCLUDE_DIR stai_mpu_network.h
        PATHS
            ${CMAKE_SYSROOT}/usr/include/stai_mpu
            /usr/include/stai_mpu
            /usr/local/include/stai_mpu)
    if(NOT STAI_MPU_INCLUDE_DIR)
        message(FATAL_ERROR "EDGEFOCUS_WITH_STAI_MPU=ON but stai_mpu_network.h was not found")
    endif()
    target_compile_definitions(focus_app PRIVATE EDGEFOCUS_WITH_STAI_MPU)
    target_sources(edgefocus_distraction_core PRIVATE
        core/distraction/src/stai_pose_runtime.cpp)
    target_compile_definitions(edgefocus_distraction_core PUBLIC DISTRACTION_WITH_STAI_MPU)
    target_include_directories(edgefocus_distraction_core PRIVATE ${STAI_MPU_INCLUDE_DIR})
    target_link_libraries(edgefocus_distraction_core PUBLIC stai_mpu)
    target_include_directories(focus_app PRIVATE
        ${STAI_MPU_INCLUDE_DIR})
    target_link_libraries(focus_app PRIVATE stai_mpu)
endif()

if(UNIX AND NOT APPLE)
    find_package(PkgConfig)
    if(PkgConfig_FOUND)
        pkg_check_modules(GIO QUIET gio-2.0 gobject-2.0 glib-2.0)
        if(GIO_FOUND)
            target_compile_definitions(focus_app PRIVATE EDGEFOCUS_WITH_BLUEZ)
            target_include_directories(focus_app PRIVATE ${GIO_INCLUDE_DIRS})
            target_link_libraries(focus_app PRIVATE ${GIO_LIBRARIES})
            target_link_directories(focus_app PRIVATE ${GIO_LIBRARY_DIRS})
            target_compile_options(focus_app PRIVATE ${GIO_CFLAGS_OTHER})
            message(STATUS "BlueZ GATT transport enabled via GIO")
        else()
            message(WARNING "GIO/glib not found; BlueZ GATT transport disabled")
        endif()
    else()
        message(WARNING "pkg-config not found; BlueZ GATT transport disabled")
    endif()
endif()

# Link pthreads (required for std::thread on Linux/ARM)
find_package(Threads REQUIRED)
target_link_libraries(focus_app PRIVATE Threads::Threads)
if(TARGET edgefocus-fan-bridge)
    target_link_libraries(edgefocus-fan-bridge PRIVATE Threads::Threads)
endif()

# Install target
install(TARGETS focus_app DESTINATION bin)
install(TARGETS gimbal_bringup DESTINATION bin)
if(TARGET edgefocus-fan-bridge)
    install(TARGETS edgefocus-fan-bridge DESTINATION bin)
    install(FILES platform/fan/bridge/edgefocus-fan-bridge.service
        DESTINATION share/edgefocus/systemd)
endif()
install(DIRECTORY config DESTINATION .)
install(DIRECTORY models/distraction DESTINATION models)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/assets/demo)
    install(DIRECTORY assets/demo DESTINATION assets)
endif()

# --- Tests ---
enable_testing()
add_subdirectory(tests)
