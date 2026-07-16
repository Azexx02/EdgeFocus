cmake_minimum_required(VERSION 3.14)
project(edgefocus_fan LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(EDGEFOCUS_FAN_BUILD_BRIDGE "Build Linux RPMsg temperature bridge" ON)
option(EDGEFOCUS_FAN_BUILD_TESTS "Build host tests" ON)

function(edgefocus_strict_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

add_library(edgefocus_fan_core STATIC
    core/FanControlCore.cpp
    core/FanProtocol.cpp)
target_include_directories(edgefocus_fan_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/core)
edgefocus_strict_warnings(edgefocus_fan_core)

add_library(edgefocus_m33_fan_app STATIC
    ../../firmware/m33/fan_controller/FanControllerApp.cpp)
target_include_directories(edgefocus_m33_fan_app PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../firmware/m33/fan_controller)
target_link_libraries(edgefocus_m33_fan_app PUBLIC edgefocus_fan_core)
edgefocus_strict_warnings(edgefocus_m33_fan_app)

if(EDGEFOCUS_FAN_BUILD_BRIDGE AND UNIX AND NOT APPLE)
    find_package(Threads REQUIRED)
    add_executable(edgefocus-fan-bridge
        bridge/FanBridge.cpp
        bridge/FrameStreamDecoder.cpp
        bridge/RpmsgTransport.cpp
        bridge/ThermalSource.cpp)
    target_include_directories(edgefocus-fan-bridge PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/bridge)
    target_link_libraries(edgefocus-fan-bridge PRIVATE
        edgefocus_fan_core Threads::Threads)
    edgefocus_strict_warnings(edgefocus-fan-bridge)
    install(TARGETS edgefocus-fan-bridge DESTINATION bin)
    install(FILES bridge/edgefocus-fan-bridge.service
        DESTINATION share/edgefocus/systemd)
    install(PROGRAMS board/edgefocus_fan_transition.py
        DESTINATION sbin
        RENAME edgefocus-fan-transition)
    install(FILES
        board/edgefocus-m33-fan.service
        board/edgefocus-fan-healthcheck.service
        board/edgefocus-fan-fallback.service
        DESTINATION share/edgefocus/systemd)
    install(FILES board/fan-calibration.json
        DESTINATION share/edgefocus)
endif()

if(EDGEFOCUS_FAN_BUILD_TESTS)
    enable_testing()

    add_executable(test_fan_control tests/test_fan_control.cpp)
    target_link_libraries(test_fan_control PRIVATE edgefocus_fan_core)
    edgefocus_strict_warnings(test_fan_control)
    add_test(NAME test_fan_control COMMAND test_fan_control)

    add_executable(test_fan_protocol tests/test_fan_protocol.cpp)
    target_link_libraries(test_fan_protocol PRIVATE edgefocus_fan_core)
    edgefocus_strict_warnings(test_fan_protocol)
    add_test(NAME test_fan_protocol COMMAND test_fan_protocol)

    add_executable(test_m33_fan_controller tests/test_m33_fan_controller.cpp)
    target_link_libraries(test_m33_fan_controller PRIVATE edgefocus_m33_fan_app)
    edgefocus_strict_warnings(test_m33_fan_controller)
    add_test(NAME test_m33_fan_controller COMMAND test_m33_fan_controller)

    if(UNIX AND NOT APPLE)
        add_executable(test_fan_bridge
            tests/test_fan_bridge.cpp
            bridge/FrameStreamDecoder.cpp
            bridge/ThermalSource.cpp)
        target_include_directories(test_fan_bridge PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/bridge)
        target_link_libraries(test_fan_bridge PRIVATE edgefocus_fan_core)
        edgefocus_strict_warnings(test_fan_bridge)
        add_test(NAME test_fan_bridge COMMAND test_fan_bridge)
    endif()
endif()
