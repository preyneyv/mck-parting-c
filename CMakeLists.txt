cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

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

include(pico_sdk_import.cmake)
project(mck-parting-c C CXX ASM)
pico_sdk_init()

# Add the u8g2 graphics library
add_subdirectory(lib/u8g2)

set(COMMON_SOURCES
    src/common/audio.c
)

set(COMMON_LIBRARIES
    u8g2
    m
)

if(NOT CMAKE_GENERATOR STREQUAL "Ninja")
    message(WARNING "Consider using Ninja generator for faster builds: cmake -G Ninja [...]")
endif()

if(PICO_PLATFORM STREQUAL "host")
    # Host build (utilities for testing and development)
    file(GLOB HOST_ENTRYPOINTS "src/host/test_*.c")

    foreach(test_source ${HOST_ENTRYPOINTS})
        get_filename_component(test_name ${test_source} NAME_WE)

        add_executable(${test_name}
            ${test_source}
            ${COMMON_SOURCES}
        )

        target_link_libraries(${test_name} ${COMMON_LIBRARIES})

        target_include_directories(${test_name} PRIVATE
            ${CMAKE_CURRENT_LIST_DIR}/src/
        )

        target_compile_options(${test_name} PRIVATE -ffast-math)
    endforeach()
else()
    # Final device build (rpi)
    set(PICO_BOARD pico CACHE STRING "Board type")

    add_executable(mck-parting-c
        src/rp2/main.c
        ${COMMON_SOURCES}
    )

    target_link_libraries(mck-parting-c
        pico_stdlib
        pico_multicore
        hardware_spi
        hardware_pwm
        ${COMMON_LIBRARIES}
    )

    target_include_directories(mck-parting-c PRIVATE
            ${CMAKE_CURRENT_LIST_DIR}/src
    )

    target_compile_options(mck-parting-c PRIVATE -ffast-math)

    pico_enable_stdio_uart(mck-parting-c 0)
    pico_enable_stdio_usb(mck-parting-c 1)

    pico_set_program_name(mck-parting-c "mck-parting-c")
    pico_set_program_version(mck-parting-c "0.1")

    pico_add_extra_outputs(mck-parting-c)
endif()
