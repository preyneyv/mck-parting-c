# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.1.1)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.1.1)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(mck-parting-c C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Add the u8g2 graphics library
add_subdirectory(lib/u8g2)

# Sources in src/
add_executable(mck-parting-c
        src/main.c
        src/audio.c
)

pico_set_program_name(mck-parting-c "mck-parting-c")
pico_set_program_version(mck-parting-c "0.1")

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(mck-parting-c 0)
pico_enable_stdio_usb(mck-parting-c 1)

# Add the standard library to the build
target_link_libraries(mck-parting-c
        pico_stdlib
        pico_multicore
        hardware_spi
        hardware_pwm
        m
        u8g2
        )

# Add the standard include files to the build
target_include_directories(mck-parting-c PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/src
)

pico_add_extra_outputs(mck-parting-c)
