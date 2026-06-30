cmake_minimum_required(VERSION 3.14)
project(focus_app LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/utf-8)
endif()

option(EDGEFOCUS_WITH_OPENCV "Build camera capture and CPU/STAI preprocessing with OpenCV" OFF)
option(EDGEFOCUS_WITH_STAI_MPU "Build STAI_MPU pose model adapter" OFF)

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

# All sources from app/, camera/, vision/, behavior/, policy/, device/, storage/, ui/
set(APP_SOURCES
    main.cpp
    app/Logger.cpp
    app/AppContext.cpp
    app/SystemStateStore.cpp
    app/SessionController.cpp
    camera/CameraPipeline.cpp
    vision/FrameQuality.cpp
    vision/ModelRegistry.cpp
    vision/PoseModelAdapter.cpp
    vision/MockVisionSource.cpp
    vision/InferenceScheduler.cpp
    vision/VisionEngine.cpp
    behavior/BehaviorEngine.cpp
    behavior/FocusStateMachine.cpp
    behavior/SlidingWindow.cpp
    behavior/EventDetector.cpp
    behavior/RiskScorer.cpp
    policy/ReminderPolicy.cpp
    policy/NotificationRouter.cpp
    policy/ModeManager.cpp
    device/GpioHelper.cpp
    device/DeviceManager.cpp
    device/ScreenController.cpp
    device/BleBandController.cpp
    storage/SQLiteStore.cpp
    storage/ReportGenerator.cpp
    network/AppCommandService.cpp
    network/BleGattServer.cpp
    ui/UIManager.cpp
    ui/FrameBuffer.cpp
    ui/StatusPage.cpp
    ui/StatsPage.cpp
    ui/ReviewPage.cpp
    ui/SettingsPage.cpp
)

add_executable(focus_app ${APP_SOURCES})
target_include_directories(focus_app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(focus_app PRIVATE sqlite3_lib nlohmann_json::nlohmann_json)

if(EDGEFOCUS_WITH_OPENCV)
    find_package(OpenCV REQUIRED COMPONENTS core imgproc videoio)
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

# Install target
install(TARGETS focus_app DESTINATION bin)
install(DIRECTORY config DESTINATION .)

# --- Tests ---
enable_testing()
add_subdirectory(tests)
