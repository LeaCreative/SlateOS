set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR cortex-m4)

set(TOOLCHAIN_PREFIX arm-none-eabi)

find_program(CMAKE_C_COMPILER ${TOOLCHAIN_PREFIX}-gcc REQUIRED)
find_program(CMAKE_CXX_COMPILER ${TOOLCHAIN_PREFIX}-g++ REQUIRED)
find_program(CMAKE_ASM_COMPILER ${TOOLCHAIN_PREFIX}-gcc REQUIRED)
find_program(CMAKE_AR ${TOOLCHAIN_PREFIX}-ar REQUIRED)
find_program(CMAKE_RANLIB ${TOOLCHAIN_PREFIX}-ranlib REQUIRED)
find_program(CMAKE_OBJCOPY ${TOOLCHAIN_PREFIX}-objcopy REQUIRED)
find_program(CMAKE_SIZE ${TOOLCHAIN_PREFIX}-size REQUIRED)

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(CMAKE_C_FLAGS_INIT "-ffreestanding -fdata-sections -ffunction-sections")
set(CMAKE_CXX_FLAGS_INIT "-ffreestanding -fdata-sections -ffunction-sections -fno-exceptions -fno-rtti")
set(CMAKE_ASM_FLAGS_INIT "-x assembler-with-cpp")
