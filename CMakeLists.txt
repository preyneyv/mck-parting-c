cmake_minimum_required(VERSION 3.13)

if(NOT CMAKE_GENERATOR STREQUAL "Ninja")
    message(WARNING "Consider using Ninja generator for faster builds: cmake -G Ninja [...]")
endif()


set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release MinSizeRel RelWithDebInfo)
endif()


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

# --- Prism cartridge SDK ---
add_library(prism_cartridge_headers INTERFACE)
target_include_directories(prism_cartridge_headers INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/src
    ${CMAKE_CURRENT_LIST_DIR}/lib/u8g2/csrc
    ${CMAKE_CURRENT_LIST_DIR}/lib/qrcodegen
)
add_library(prism_sdk INTERFACE)
target_link_libraries(prism_sdk INTERFACE prism_cartridge_headers)
target_link_libraries(prism_sdk INTERFACE u8g2 qrcodegen m)

# Cartridges are compiled as their own target. They are bundled into the
# firmware today, but only consume the public Prism SDK boundary.
set(CARTRIDGE_SOURCES
    src/cartridges/_full_test/app.c
    src/cartridges/dummy/app.c
)
if(PICO_PLATFORM STREQUAL "host")
    list(APPEND CARTRIDGE_SOURCES
        src/cartridges/bongocat/app.c
        src/cartridges/morse/app.c
        src/cartridges/asteroids/app.c
        src/cartridges/beatline/app.c
        src/cartridges/beatline/chart.c
        src/cartridges/beatline/game.c
    )
endif()
add_library(prism_cartridges OBJECT ${CARTRIDGE_SOURCES})
target_link_libraries(prism_cartridges PRIVATE prism_sdk)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/cartridge_boundary.cmake)
prism_check_cartridge_boundary(${CMAKE_CURRENT_LIST_DIR}/src/cartridges)

# --- OS and shared runtime ---
set(SHARED_SOURCES
    src/shared/audio/buffer.c
    src/shared/audio/synth.c
    src/shared/audio/song.c
    src/shared/audio/midi.c
    src/shared/leaderboard/leaderboard.c
    src/shared/anim.c
    src/shared/engine.c
    src/shared/os/launcher/app.c
    src/shared/os/cartridge_runtime.c
    src/shared/os/cartridge_registry.c
    src/shared/os/settings.c
    src/shared/os/midi_mode.c
    $<TARGET_OBJECTS:prism_cartridges>
)
add_library(shared STATIC ${SHARED_SOURCES})
target_include_directories(shared PRIVATE ${CMAKE_CURRENT_LIST_DIR}/src)
target_link_libraries(shared PUBLIC
    prism_sdk
    u8g2
    qrcodegen
    m
)
if(NOT PICO_PLATFORM STREQUAL "host")
    target_link_libraries(shared PUBLIC pico_platform)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(${CMAKE_CURRENT_LIST_DIR}/cmake/assets.cmake)

if(PICO_PLATFORM STREQUAL "host")
    # --- host build ---
    target_compile_definitions(shared PUBLIC PICO_ON_DEVICE=0)
    add_executable(mck-parting-c
        src/host/main.c
        src/host/audio.c
        src/host/display.c
        src/host/platform.c
        src/host/input.c
        src/host/leds.c
        src/host/time.c
        src/host/system.c
        src/host/identity.c
        src/host/peripheral.c
        src/host/persistence.c
        src/host/cartridge.c
        src/host/sleep.c
        src/host/sync.c
    )

    # force-include the host compatibility header
    target_compile_options(shared PRIVATE -include ${CMAKE_CURRENT_LIST_DIR}/src/host/compat.h)
    target_compile_options(mck-parting-c PRIVATE -include ${CMAKE_CURRENT_LIST_DIR}/src/host/compat.h)

    target_include_directories(mck-parting-c PRIVATE ${CMAKE_CURRENT_LIST_DIR}/src)
    # target_compile_options(mck-parting-c PRIVATE -ffast-math)

    find_package(Threads REQUIRED)
    # Link SDL statically so the host simulator is a clone-and-run executable.
    find_package(SDL2 CONFIG REQUIRED)

    target_link_libraries(mck-parting-c PRIVATE
        shared
        SDL2::SDL2-static
        Threads::Threads
    )
    if(MINGW)
        target_link_options(mck-parting-c PRIVATE -static -static-libgcc)
    endif()



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
        src/rp2/management.c
        src/rp2/cartridge_storage.c
        src/rp2/cartridge.c
        src/rp2/cartridge_call.S
        src/rp2/platform.c
        src/rp2/input.c
        src/rp2/sleep.c
        src/rp2/time.c
        src/rp2/system.c
        src/rp2/identity.c
        src/rp2/display.c
        src/rp2/audio.c
        src/rp2/peripheral.c
        src/rp2/persistence.c
        src/rp2/leds.c
        src/rp2/sync.c
        src/rp2/usb_descriptors.c
    )

    target_compile_definitions(mck-parting-c PUBLIC
        # The sole vendor-class interface belongs to Prism management. Picotool
        # can still reboot through the CDC baud-rate convention.
        PICO_STDIO_USB_ENABLE_RESET_VIA_VENDOR_INTERFACE=0
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

    # Keep one real installable package in the main build as an end-to-end SDK
    # fixture.  The standalone hello repository uses this same CMake helper.
    set(PRISM_SDK_ROOT ${CMAKE_CURRENT_LIST_DIR})
    include(${CMAKE_CURRENT_LIST_DIR}/cmake/PrismCartridge.cmake)
    prism_add_installable_cartridge(hello_cartridge
        UUID "7e537f9b-9314-4e27-a0b1-219f48ff88d4"
        ENTRY cartridge_hello
        SOURCES
            ${CMAKE_CURRENT_LIST_DIR}/examples/hello-cartridge/app.c
        INCLUDE_DIRECTORIES
            ${CMAKE_CURRENT_LIST_DIR}/examples/hello-cartridge
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/hello.prism
    )

    prism_add_installable_cartridge(bongocat_cartridge
        UUID "7af8d2af-50cb-4e4b-a203-5efa9bdd61e6"
        ENTRY cartridge_bongocat
        SOURCES
            ${CMAKE_CURRENT_LIST_DIR}/src/cartridges/bongocat/app.c
        INCLUDE_DIRECTORIES
            ${CMAKE_CURRENT_LIST_DIR}/src/cartridges/bongocat
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/bongocat.prism
    )

    prism_add_installable_cartridge(morse_cartridge
        UUID "05650047-0614-4ff4-8d46-e34fc28b3fd9"
        ENTRY cartridge_morse
        SOURCES
            ${CMAKE_CURRENT_LIST_DIR}/src/cartridges/morse/app.c
        INCLUDE_DIRECTORIES
            ${CMAKE_CURRENT_LIST_DIR}/src/cartridges/morse
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/morse.prism
    )

    prism_add_installable_cartridge(asteroids_cartridge
        UUID "43b03ee8-80f2-4e53-a8fe-e09a5cd04c75"
        ENTRY cartridge_asteroids
        SOURCES
            ${CMAKE_CURRENT_LIST_DIR}/src/cartridges/asteroids/app.c
        INCLUDE_DIRECTORIES
            ${CMAKE_CURRENT_LIST_DIR}/src/cartridges/asteroids
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/asteroids.prism
    )

    prism_add_installable_cartridge(beatline_cartridge
        UUID "36ea78d2-ca5e-4bed-bd9b-c4e90dca4c26"
        ENTRY cartridge_beatline
        SOURCES
            ${CMAKE_CURRENT_LIST_DIR}/src/cartridges/beatline/app.c
            ${CMAKE_CURRENT_LIST_DIR}/src/cartridges/beatline/chart.c
            ${CMAKE_CURRENT_LIST_DIR}/src/cartridges/beatline/game.c
        INCLUDE_DIRECTORIES
            ${CMAKE_CURRENT_LIST_DIR}/src/cartridges/beatline
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/beatline.prism
    )

    set(DEFAULT_CARTRIDGE_ASM ${CMAKE_CURRENT_BINARY_DIR}/default_cartridges.S)
    add_custom_command(
        OUTPUT ${DEFAULT_CARTRIDGE_ASM}
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_LIST_DIR}/scripts/embed_default_cartridges.py
                --output ${DEFAULT_CARTRIDGE_ASM}
                bongocat=${bongocat_cartridge_PRISM_FILE}
                morse=${morse_cartridge_PRISM_FILE}
                asteroids=${asteroids_cartridge_PRISM_FILE}
                beatline=${beatline_cartridge_PRISM_FILE}
        DEPENDS
            ${bongocat_cartridge_PRISM_FILE}
            ${morse_cartridge_PRISM_FILE}
            ${asteroids_cartridge_PRISM_FILE}
            ${beatline_cartridge_PRISM_FILE}
            ${CMAKE_CURRENT_LIST_DIR}/scripts/embed_default_cartridges.py
        VERBATIM
        COMMENT "Embedding default Prism cartridges"
    )
    set_source_files_properties(${DEFAULT_CARTRIDGE_ASM} PROPERTIES GENERATED TRUE)
    target_sources(mck-parting-c PRIVATE ${DEFAULT_CARTRIDGE_ASM})
endif()
