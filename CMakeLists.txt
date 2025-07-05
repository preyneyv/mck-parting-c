cmake_minimum_required(VERSION 3.13)

if(NOT CMAKE_GENERATOR STREQUAL "Ninja")
    message(WARNING "Consider using Ninja generator for faster builds: cmake -G Ninja [...]")
endif()

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

# --- shared dependencies ---
add_subdirectory(lib/u8g2)

# --- shared lib ---
set(SHARED_SOURCES
    src/shared/audio/buffer.c
    src/shared/display.c
)
add_library(shared STATIC ${SHARED_SOURCES})
target_include_directories(shared PRIVATE ${CMAKE_CURRENT_LIST_DIR}/src)
target_link_libraries(shared PUBLIC u8g2 m pico_stdlib)

if(PICO_PLATFORM STREQUAL "host")
    # --- host build ---
    add_executable(mck-parting-c
        src/host/main.c
        src/host/audio.c
        src/host/display.c
    )

    # force-include the host compatibility header
    target_compile_options(shared PRIVATE -include ${CMAKE_CURRENT_LIST_DIR}/src/host/compat.h)
    target_compile_options(mck-parting-c PRIVATE -include ${CMAKE_CURRENT_LIST_DIR}/src/host/compat.h)
    

    find_package(Threads REQUIRED)
    find_package(SDL2 REQUIRED CONFIG REQUIRED COMPONENTS SDL2)
    target_link_libraries(mck-parting-c PRIVATE
        shared
        SDL2::SDL2
        Threads::Threads
    )
    target_include_directories(mck-parting-c PRIVATE ${CMAKE_CURRENT_LIST_DIR}/src)
    target_compile_options(mck-parting-c PRIVATE -ffast-math)


    # file(GLOB U8G2_SYS_SDL "lib/u8g2/sys/sdl/common/*.c")

    # --- host executable targets ---
    # file(GLOB host_entrypoints CONFIGURE_DEPENDS "src/host/test_*.c")
    # foreach(ts IN LISTS host_entrypoints)
    #     get_filename_component(test_name ${ts} NAME_WE)
    #     add_executable(${test_name} ${ts} ${U8G2_SYS_SDL})
    #     target_link_libraries(${test_name} PRIVATE 
    #         shared 
    #         SDL2::SDL2
    #     )
    #     target_include_directories(${test_name} PRIVATE ${CMAKE_CURRENT_LIST_DIR}/src)
    #     target_compile_options(${test_name} PRIVATE -ffast-math)
    # endforeach()
else()
    # --- rp2 build ---
    set(PICO_BOARD pico CACHE STRING "Board type")
    add_executable(mck-parting-c
        src/rp2/main.c
        src/rp2/display.c
        src/rp2/audio.c
    )
    target_link_libraries(mck-parting-c PRIVATE
        shared
        pico_stdlib
        pico_multicore
        hardware_spi
        hardware_pwm
    )
    target_include_directories(mck-parting-c PRIVATE ${CMAKE_CURRENT_LIST_DIR}/src)
    target_compile_options(mck-parting-c PRIVATE -ffast-math)


    pico_enable_stdio_uart(mck-parting-c 0)
    pico_enable_stdio_usb(mck-parting-c 1)
    pico_set_program_name(mck-parting-c "mck-parting-c")
    pico_set_program_version(mck-parting-c "0.1")
    pico_add_extra_outputs(mck-parting-c)
endif()
