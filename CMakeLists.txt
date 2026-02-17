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
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if(EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================

set(PICO_BOARD board)
list(APPEND PICO_BOARD_HEADER_DIRS ${CMAKE_CURRENT_LIST_DIR})

include(pico_sdk_import.cmake)
project(mck-parting-c C CXX ASM)

pico_sdk_init()

# super important for u8g2 otherwise we get a 11 MB binary :skull:
add_compile_options(-ffast-math -ffunction-sections -fdata-sections)
if(APPLE)
    add_link_options(-Wl,-dead_strip)
else()
    add_link_options(-Wl,--gc-sections)
endif()

# --- shared dependencies ---
add_subdirectory(lib/u8g2)
add_subdirectory(lib/qrcodegen)

# --- shared lib ---
set(SHARED_SOURCES
    src/shared/audio/buffer.c
    src/shared/audio/synth.c
    src/shared/audio/midi.c
    src/shared/leaderboard/leaderboard.c
    src/shared/anim.c
    src/shared/engine.c
    src/shared/apps/_launcher/app.c
    src/shared/apps/_full_test/app.c
    src/shared/apps/bongocat/app.c
    src/shared/apps/dummy/app.c
    src/shared/apps/morse/app.c
    # src/shared/apps/rhythm/app.c
    # src/shared/apps/rhythm/tracker.c
    src/shared/apps/asteroids/app.c
)
add_library(shared STATIC ${SHARED_SOURCES})
target_include_directories(shared PRIVATE ${CMAKE_CURRENT_LIST_DIR}/src)
target_link_libraries(shared PUBLIC
    u8g2
    qrcodegen
    m
)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(${CMAKE_CURRENT_LIST_DIR}/cmake/assets.cmake)

if(PICO_PLATFORM STREQUAL "host")
    # --- host build ---
    add_executable(mck-parting-c
        src/host/main.c
        src/host/audio.c
        src/host/display.c
        src/host/platform.c
        src/host/platform_sync.c
    )

    # force-include the host compatibility header
    target_compile_options(shared PRIVATE -include ${CMAKE_CURRENT_LIST_DIR}/src/host/compat.h)
    target_compile_options(mck-parting-c PRIVATE -include ${CMAKE_CURRENT_LIST_DIR}/src/host/compat.h)

    target_include_directories(mck-parting-c PRIVATE ${CMAKE_CURRENT_LIST_DIR}/src)
    # target_compile_options(mck-parting-c PRIVATE -ffast-math)

    find_package(Threads REQUIRED)
    find_package(SDL2 REQUIRED CONFIG REQUIRED COMPONENTS SDL2)

    # Find libsoundio manually
    find_path(SOUNDIO_INCLUDE_DIR
        NAMES soundio/soundio.h
        PATHS /opt/homebrew/include /usr/local/include
    )
    find_library(SOUNDIO_LIBRARY
        NAMES soundio
        PATHS /opt/homebrew/lib /usr/local/lib
    )

    if(NOT SOUNDIO_INCLUDE_DIR OR NOT SOUNDIO_LIBRARY)
        message(FATAL_ERROR "libsoundio not found. Install with: brew install libsoundio")
    endif()

    target_link_libraries(mck-parting-c PRIVATE
        shared
        SDL2::SDL2
        Threads::Threads
        ${SOUNDIO_LIBRARY}
        pico_stdlib
    )
    target_include_directories(mck-parting-c PRIVATE
        ${SOUNDIO_INCLUDE_DIR}
    )



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
    add_executable(mck-parting-c
        src/rp2/main.c
        src/rp2/midi.c
        src/rp2/display.c
        src/rp2/audio.c
        src/rp2/peripheral.c
        src/rp2/leds.c
        src/rp2/platform.c
        src/rp2/platform_sync.c
        src/rp2/usb_descriptors.c
    )

    target_compile_definitions(mck-parting-c PUBLIC
        PICO_STDIO_USB_ENABLE_RESET_VIA_VENDOR_INTERFACE=1
        PICO_STDIO_USB_ENABLE_RESET_VIA_BAUD_RATE=1
        PICO_STDIO_USB_ENABLE_IRQ_BACKGROUND_TASK=1
        PICO_STDIO_USB_ENABLE_TINYUSB_INIT=1
    )

    target_link_libraries(mck-parting-c PRIVATE
        shared
        hardware_adc
        hardware_clocks
        hardware_divider
        hardware_dma
        hardware_irq
        hardware_pio
        hardware_pwm
        hardware_spi
        hardware_sync
        hardware_watchdog
        pico_multicore
        pico_rand
        pico_stdlib
        pico_unique_id
        tinyusb_device
        tinyusb_board
    )
    target_include_directories(mck-parting-c PRIVATE ${CMAKE_CURRENT_LIST_DIR}/src)

    pico_generate_pio_header(mck-parting-c ${CMAKE_CURRENT_LIST_DIR}/src/rp2/audio.pio)
    pico_generate_pio_header(mck-parting-c ${CMAKE_CURRENT_LIST_DIR}/src/rp2/leds.pio)


    pico_enable_stdio_uart(mck-parting-c 0)
    pico_enable_stdio_usb(mck-parting-c 1)
    pico_set_program_name(mck-parting-c "mck-parting-c")
    pico_set_program_version(mck-parting-c "0.1")
    pico_add_extra_outputs(mck-parting-c)
endif()
