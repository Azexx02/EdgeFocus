cmake_minimum_required(VERSION 3.22)
project(gimbal_core_tests C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

enable_testing()

add_library(gimbal_core STATIC
    ../Core/Src/gimbal_angle.c
    ../Core/Src/gimbal_power_profile.c
    ../Core/Src/gimbal_nv.c
    ../Core/Src/gimbal_protocol.c)
target_sources(gimbal_core PRIVATE ../Core/Src/gimbal_rx.c)
target_include_directories(gimbal_core PUBLIC ../Core/Inc)

add_executable(test_gimbal_core test_gimbal_core.c)
target_link_libraries(test_gimbal_core PRIVATE gimbal_core)
add_test(NAME test_gimbal_core COMMAND test_gimbal_core)

if(MSVC)
    target_compile_options(gimbal_core PRIVATE /W4 /WX)
    target_compile_options(test_gimbal_core PRIVATE /W4 /WX)
else()
    target_compile_options(gimbal_core PRIVATE -Wall -Wextra -Werror)
    target_compile_options(test_gimbal_core PRIVATE -Wall -Wextra -Werror)
endif()
