cmake_minimum_required(VERSION 3.20)
project(EdgeFocus_FanController_CM33_NonSecure LANGUAGES C CXX ASM)

if(NOT DEFINED ATK_ROOT OR NOT DEFINED STAGE_ROOT)
    message(FATAL_ERROR "ATK_ROOT and STAGE_ROOT are required")
endif()

file(TO_CMAKE_PATH "${ATK_ROOT}" ATK_ROOT)
file(TO_CMAKE_PATH "${STAGE_ROOT}" STAGE_ROOT)

set(CORE_ROOT "${STAGE_ROOT}/CM33/NonSecure")
set(OPENAMP_ROOT "${ATK_ROOT}/Middlewares/Third_Party/OpenAMP")
set(HAL_ROOT "${ATK_ROOT}/Drivers/STM32MP2xx_HAL_Driver")
set(CMSIS_ROOT "${ATK_ROOT}/Drivers/CMSIS")

# Generate libmetal's target-specific config header and build the same generic
# bare-metal implementation used by ST's OpenAMP example.  CPU flags come from
# the toolchain file and therefore apply to libmetal too.
set(WITH_DEFAULT_LOGGER OFF CACHE BOOL "" FORCE)
set(MACHINE template CACHE STRING "libmetal machine implementation" FORCE)
add_subdirectory("${OPENAMP_ROOT}/libmetal" libmetal-build EXCLUDE_FROM_ALL)

# The vendor CubeIDE project compiles libmetal with the same CM33/OpenAMP
# contract as the application.  CMake's standalone libmetal defaults omit
# several of these target definitions, so propagate them explicitly.
target_compile_definitions(metal-static PRIVATE
    STM32MP257Cxx
    CORE_CM33
    USE_STM32MP257F_EV1
    USE_STM32MP257_ATK
    USE_HAL_DRIVER
    NO_ATOMIC_64_SUPPORT
    VIRTIO_DEVICE_ONLY
    METAL_MAX_DEVICE_REGIONS=2
    LINUX_RPROC_MASTER
)
target_compile_options(metal-static PRIVATE -Os -ffunction-sections -Wall)

set(FIRMWARE_SOURCES
    "${CORE_ROOT}/Core/Src/main.c"
    "${CORE_ROOT}/Core/Src/stm32mp2xx_hal_msp.c"
    "${CORE_ROOT}/Core/Src/stm32mp2xx_it.c"
    "${CORE_ROOT}/Core/Src/FanControlCore.cpp"
    "${CORE_ROOT}/Core/Src/FanControllerApp.cpp"
    "${CORE_ROOT}/Core/Src/FanProtocol.cpp"
    "${CORE_ROOT}/Core/Src/edgefocus_fan_board.cpp"
    "${CORE_ROOT}/OPENAMP/mbox_ipcc.c"
    "${CORE_ROOT}/OPENAMP/openamp.c"
    "${CORE_ROOT}/OPENAMP/openamp_log.c"
    "${CORE_ROOT}/OPENAMP/rsc_table.c"
    "${CMSIS_ROOT}/Device/ST/STM32MP2xx/Source/Templates/gcc/startup_stm32mp257cxx_m33.s"
    "${ATK_ROOT}/Utilities/Services/GenericServices/syscalls.c"
    "${ATK_ROOT}/Utilities/CoproSync/copro_sync.c"
    "${ATK_ROOT}/Utilities/ResourcesManager/res_mgr.c"
    "${ATK_ROOT}/Drivers/BSP/STM32MP257F-EV1/stm32mp257f_eval.c"
    "${CMSIS_ROOT}/Device/ST/STM32MP2xx/Source/Templates/system_stm32mp2xx_m33_ns.c"
    "${HAL_ROOT}/Src/stm32mp2xx_hal.c"
    "${HAL_ROOT}/Src/stm32mp2xx_hal_cortex.c"
    "${HAL_ROOT}/Src/stm32mp2xx_hal_dma.c"
    "${HAL_ROOT}/Src/stm32mp2xx_hal_dma_ex.c"
    "${HAL_ROOT}/Src/stm32mp2xx_hal_exti.c"
    "${HAL_ROOT}/Src/stm32mp2xx_hal_gpio.c"
    "${HAL_ROOT}/Src/stm32mp2xx_hal_icache.c"
    "${HAL_ROOT}/Src/stm32mp2xx_hal_ipcc.c"
    "${HAL_ROOT}/Src/stm32mp2xx_hal_pwr.c"
    "${HAL_ROOT}/Src/stm32mp2xx_hal_pwr_ex.c"
    "${HAL_ROOT}/Src/stm32mp2xx_hal_rcc.c"
    "${HAL_ROOT}/Src/stm32mp2xx_hal_rcc_ex.c"
    "${HAL_ROOT}/Src/stm32mp2xx_hal_tim.c"
    "${HAL_ROOT}/Src/stm32mp2xx_hal_tim_ex.c"
    "${HAL_ROOT}/Src/stm32mp2xx_hal_uart.c"
    "${HAL_ROOT}/Src/stm32mp2xx_hal_uart_ex.c"
    "${OPENAMP_ROOT}/open-amp/lib/remoteproc/remoteproc_virtio.c"
    "${OPENAMP_ROOT}/open-amp/lib/rpmsg/rpmsg.c"
    "${OPENAMP_ROOT}/open-amp/lib/rpmsg/rpmsg_virtio.c"
    "${OPENAMP_ROOT}/open-amp/lib/virtio/virtio.c"
    "${OPENAMP_ROOT}/open-amp/lib/virtio/virtqueue.c"
    "${OPENAMP_ROOT}/virtual_driver/virt_uart.c"
)

add_executable(edgefocus_fan ${FIRMWARE_SOURCES})
set_target_properties(edgefocus_fan PROPERTIES
    OUTPUT_NAME "EdgeFocus_FanController_CM33_NonSecure"
    SUFFIX ".elf"
    C_STANDARD 11
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
    # Match CubeIDE's arm-none-eabi-gcc link driver.  C++ runtime libraries
    # remain explicit below, avoiding a g++-specific startup/link layout.
    LINKER_LANGUAGE C
)

target_compile_definitions(edgefocus_fan PRIVATE
    STM32MP257Cxx
    CORE_CM33
    USE_STM32MP257F_EV1
    USE_STM32MP257_ATK
    USE_HAL_DRIVER
    NO_ATOMIC_64_SUPPORT
    METAL_INTERNAL
    VIRTIO_DEVICE_ONLY
    METAL_MAX_DEVICE_REGIONS=2
    LINUX_RPROC_MASTER
)

target_include_directories(edgefocus_fan PRIVATE
    "${CORE_ROOT}/Core/Inc"
    "${CORE_ROOT}/OPENAMP"
    "${ATK_ROOT}/Utilities/ResourcesManager"
    "${ATK_ROOT}/Utilities/CoproSync"
    "${OPENAMP_ROOT}/open-amp/lib/include"
    "${OPENAMP_ROOT}/virtual_driver"
    "${CMAKE_BINARY_DIR}/libmetal-build/lib/include"
    "${CMSIS_ROOT}/Device/ST/STM32MP2xx/Include"
    "${CMSIS_ROOT}/Core/Include"
    "${HAL_ROOT}/Inc"
    "${ATK_ROOT}/Drivers/BSP/STM32MP257F-EV1"
)

target_compile_options(edgefocus_fan PRIVATE
    -Os
    -ffunction-sections
    -Wall
    $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>
    $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>
    $<$<COMPILE_LANGUAGE:CXX>:-fno-use-cxa-atexit>
)

set(LINKER_SCRIPT "${CMSIS_ROOT}/Device/ST/STM32MP2xx/Source/Templates/gcc/linker/stm32mp2xx_DDR_m33_ns.ld")
target_link_options(edgefocus_fan PRIVATE
    "-T${LINKER_SCRIPT}"
    --specs=nosys.specs
    -static
    "-Wl,-z,max-page-size=4096"
    -Wl,--gc-sections
    "-Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/EdgeFocus_FanController_CM33_NonSecure.map"
    -Wl,--start-group
    -lc
    -lm
    -lstdc++
    -lsupc++
    -Wl,--end-group
)
target_link_libraries(edgefocus_fan PRIVATE metal-static)

add_custom_command(TARGET edgefocus_fan POST_BUILD
    COMMAND "${CMAKE_OBJCOPY}" -S "$<TARGET_FILE:edgefocus_fan>"
            "${CMAKE_CURRENT_BINARY_DIR}/EdgeFocus_FanController_CM33_NonSecure_stripped.elf"
    COMMAND "${CMAKE_SIZE}" "$<TARGET_FILE:edgefocus_fan>"
    VERBATIM
)
