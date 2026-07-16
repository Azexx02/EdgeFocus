set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

if(NOT DEFINED ARM_GCC_ROOT)
    message(FATAL_ERROR "ARM_GCC_ROOT must point to the GNU Tools for STM32 bin directory")
endif()

file(TO_CMAKE_PATH "${ARM_GCC_ROOT}" ARM_GCC_ROOT)
set(ARM_GCC_ROOT "${ARM_GCC_ROOT}" CACHE PATH "GNU Tools for STM32 bin directory" FORCE)
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES ARM_GCC_ROOT)
set(CMAKE_C_COMPILER "${ARM_GCC_ROOT}/arm-none-eabi-gcc.exe")
set(CMAKE_CXX_COMPILER "${ARM_GCC_ROOT}/arm-none-eabi-g++.exe")
set(CMAKE_ASM_COMPILER "${ARM_GCC_ROOT}/arm-none-eabi-gcc.exe")
set(CMAKE_AR "${ARM_GCC_ROOT}/arm-none-eabi-ar.exe")
set(CMAKE_RANLIB "${ARM_GCC_ROOT}/arm-none-eabi-ranlib.exe")
set(CMAKE_OBJCOPY "${ARM_GCC_ROOT}/arm-none-eabi-objcopy.exe")
set(CMAKE_OBJDUMP "${ARM_GCC_ROOT}/arm-none-eabi-objdump.exe")
set(CMAKE_SIZE "${ARM_GCC_ROOT}/arm-none-eabi-size.exe")

set(EDGEFOCUS_CPU_FLAGS
    -mcpu=cortex-m33
    -mfpu=fpv5-sp-d16
    -mfloat-abi=hard
    -mthumb
    --specs=nano.specs
)
string(JOIN " " EDGEFOCUS_CPU_FLAGS_STRING ${EDGEFOCUS_CPU_FLAGS})
set(CMAKE_C_FLAGS_INIT "${EDGEFOCUS_CPU_FLAGS_STRING}")
set(CMAKE_CXX_FLAGS_INIT "${EDGEFOCUS_CPU_FLAGS_STRING}")
set(CMAKE_ASM_FLAGS_INIT "${EDGEFOCUS_CPU_FLAGS_STRING}")
