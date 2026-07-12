cmake_minimum_required(VERSION 3.13)

if(NOT CMAKE_GENERATOR STREQUAL "Ninja")
    message(WARNING "Consider using Ninja generator for faster builds: cmake -G Ninja [...]")
endif()


set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(PRISM_ENABLE_PERFORMANCE_LOGS
    "Collect and print frame, tick, and audio timing statistics"
    OFF
)
if(PRISM_ENABLE_PERFORMANCE_LOGS)
    add_compile_definitions(PRISM_ENABLE_PERFORMANCE_LOGS=1)
endif()

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
project(prism C CXX ASM)

pico_sdk_init()

function(prism_enable_warnings TARGET)
    target_compile_options(${TARGET} PRIVATE
        $<$<COMPILE_LANGUAGE:C>:-Wall;-Wextra;-Wformat=2;-Werror=implicit-function-declaration;-Werror=return-type>
    )
endfunction()

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

add_library(prism_sdk_header_test OBJECT tests/sdk_public_headers.c)
target_link_libraries(prism_sdk_header_test PRIVATE prism_sdk)
prism_enable_warnings(prism_sdk_header_test)

# Built-in host cartridges consume only the public Prism SDK boundary. RP2
# cartridges are packaged independently and embedded below.
set(CARTRIDGE_OBJECTS)
if(PICO_PLATFORM STREQUAL "host")
    add_library(prism_cartridges OBJECT
        src/cartridges/bongocat/app.c
        src/cartridges/morse/app.c
        src/cartridges/asteroids/app.c
        src/cartridges/beatline/app.c
        src/cartridges/beatline/chart.c
        src/cartridges/beatline/game.c
    )
    target_link_libraries(prism_cartridges PRIVATE prism_sdk)
    prism_enable_warnings(prism_cartridges)
    set(CARTRIDGE_OBJECTS $<TARGET_OBJECTS:prism_cartridges>)
endif()
include(${CMAKE_CURRENT_LIST_DIR}/cmake/cartridge_boundary.cmake)
prism_check_cartridge_boundary(${CMAKE_CURRENT_LIST_DIR}/src/cartridges)

# --- OS and shared runtime ---
set(SHARED_SOURCES
    src/shared/audio/synth.c
    src/shared/audio/song.c
    src/shared/audio/midi.c
    src/shared/leaderboard/leaderboard.c
    src/shared/anim.c
    src/shared/engine.c
    src/shared/os/launcher/app.c
    src/shared/os/cartridge_runtime.c
    src/shared/os/cartridge_registry.c
    src/shared/os/cartridge_identity.c
    src/shared/os/management_serialization.c
    src/shared/os/settings.c
    src/shared/os/midi_mode.c
    src/shared/os/pause_menu.c
    src/shared/os/power_indicator.c
    src/shared/os/wake_confirmation.c
    ${CARTRIDGE_OBJECTS}
)
add_library(shared STATIC ${SHARED_SOURCES})
prism_enable_warnings(shared)
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
    include(ExternalProject)
    set(PRISM_UNICORN_PREFIX "${CMAKE_BINARY_DIR}/unicorn")
    set(PRISM_UNICORN_INSTALL "${PRISM_UNICORN_PREFIX}/install")
    file(MAKE_DIRECTORY "${PRISM_UNICORN_INSTALL}/include")
    ExternalProject_Add(prism_unicorn
        PREFIX "${PRISM_UNICORN_PREFIX}"
        GIT_REPOSITORY https://github.com/unicorn-engine/unicorn.git
        GIT_TAG 8028ec436f2d9376525352dd38ed9ed6b9f6be10
        GIT_SHALLOW TRUE
        GIT_SUBMODULES ""
        UPDATE_COMMAND ""
        CONFIGURE_COMMAND
            ${CMAKE_COMMAND}
            -DPRISM_SOURCE=<SOURCE_DIR>
            -DPRISM_BINARY=<BINARY_DIR>
            -DPRISM_GENERATOR=${CMAKE_GENERATOR}
            -DPRISM_BUILD_TYPE=Release
            -DPRISM_INSTALL=${PRISM_UNICORN_INSTALL}
            -DPRISM_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}
            -DPRISM_C_COMPILER=${CMAKE_C_COMPILER}
            -P ${CMAKE_CURRENT_LIST_DIR}/cmake/configure_unicorn.cmake
        BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR>
        INSTALL_COMMAND ${CMAKE_COMMAND} --install <BINARY_DIR>
        BUILD_BYPRODUCTS
            "${PRISM_UNICORN_PREFIX}/src/prism_unicorn-build/libunicorn.a"
            "${PRISM_UNICORN_PREFIX}/src/prism_unicorn-build/libarm-softmmu.a"
            "${PRISM_UNICORN_PREFIX}/src/prism_unicorn-build/libunicorn-common.a"
    )
    add_library(prism_unicorn_core STATIC IMPORTED GLOBAL)
    add_library(prism_unicorn_arm STATIC IMPORTED GLOBAL)
    add_library(prism_unicorn_common STATIC IMPORTED GLOBAL)
    set_target_properties(prism_unicorn_core PROPERTIES
        IMPORTED_LOCATION "${PRISM_UNICORN_PREFIX}/src/prism_unicorn-build/libunicorn.a"
    )
    set_target_properties(prism_unicorn_arm PROPERTIES
        IMPORTED_LOCATION "${PRISM_UNICORN_PREFIX}/src/prism_unicorn-build/libarm-softmmu.a"
    )
    set_target_properties(prism_unicorn_common PROPERTIES
        IMPORTED_LOCATION "${PRISM_UNICORN_PREFIX}/src/prism_unicorn-build/libunicorn-common.a"
    )
    add_library(prism_unicorn_library INTERFACE)
    target_include_directories(prism_unicorn_library INTERFACE
        "${PRISM_UNICORN_INSTALL}/include"
    )
    target_link_libraries(prism_unicorn_library INTERFACE
        prism_unicorn_core
        prism_unicorn_arm
        prism_unicorn_common
        m
    )
    add_dependencies(prism_unicorn_library prism_unicorn)

    target_compile_definitions(shared PUBLIC PICO_ON_DEVICE=0)
    add_executable(prism
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
        src/host/cartridge_vm.c
        src/host/sleep.c
        src/host/sync.c
    )
    prism_enable_warnings(prism)

    target_compile_options(shared PRIVATE -include ${CMAKE_CURRENT_LIST_DIR}/src/host/compat.h)
    target_compile_options(prism PRIVATE -include ${CMAKE_CURRENT_LIST_DIR}/src/host/compat.h)

    target_include_directories(prism PRIVATE ${CMAKE_CURRENT_LIST_DIR}/src)

    find_package(Threads REQUIRED)
    # Link SDL statically so the host simulator is a clone-and-run executable.
    find_package(SDL2 CONFIG REQUIRED)

    target_link_libraries(prism PRIVATE
        shared
        SDL2::SDL2-static
        Threads::Threads
        prism_unicorn_library
    )
    include(CTest)
    if(BUILD_TESTING)
        add_executable(prism_cartridge_identity_tests
            tests/cartridge_identity_test.c
            src/shared/os/cartridge_identity.c
            src/shared/os/management_serialization.c
        )
        target_include_directories(prism_cartridge_identity_tests PRIVATE
            ${CMAKE_CURRENT_LIST_DIR}/src
        )
        prism_enable_warnings(prism_cartridge_identity_tests)
        add_test(NAME prism_cartridge_identity
                 COMMAND prism_cartridge_identity_tests)

        add_executable(prism_qrcode_render_tests
            tests/qrcode_render_test.c
        )
        target_link_libraries(prism_qrcode_render_tests PRIVATE prism_sdk)
        prism_enable_warnings(prism_qrcode_render_tests)
        add_test(NAME prism_qrcode_render
                 COMMAND prism_qrcode_render_tests)
    endif()
    if(MINGW)
        target_link_options(prism PRIVATE -static -static-libgcc)
    endif()
else()
    # --- rp2 build ---
    add_executable(prism
        src/rp2/main.c
        src/rp2/midi.c
        src/rp2/management.c
        src/rp2/management_commands.c
        src/rp2/management_overlay.c
        src/rp2/management_protocol.c
        src/rp2/management_transport.c
        src/rp2/cartridge_storage.c
        src/rp2/flash_io.c
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
    prism_enable_warnings(prism)

    target_compile_definitions(prism PUBLIC
        PICO_STDIO_USB_ENABLE_RESET_VIA_VENDOR_INTERFACE=1
        PICO_STDIO_USB_ENABLE_RESET_VIA_BAUD_RATE=1
        PICO_STDIO_USB_RESET_INTERFACE_SUPPORT_MS_OS_20_DESCRIPTOR=0
        PICO_STDIO_USB_ENABLE_IRQ_BACKGROUND_TASK=1
        PICO_STDIO_USB_ENABLE_TINYUSB_INIT=1
    )

    target_link_libraries(prism PRIVATE
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
    target_include_directories(prism PRIVATE ${CMAKE_CURRENT_LIST_DIR}/src)

    pico_generate_pio_header(prism ${CMAKE_CURRENT_LIST_DIR}/src/rp2/audio.pio)
    pico_generate_pio_header(prism ${CMAKE_CURRENT_LIST_DIR}/src/rp2/leds.pio)


    pico_enable_stdio_uart(prism 0)
    pico_enable_stdio_usb(prism 1)
    pico_set_program_name(prism "prism")
    pico_set_program_version(prism "1.0.0")
    pico_add_extra_outputs(prism)

    # Keep one real installable package in the main build as an end-to-end SDK
    # fixture.  The standalone hello repository uses this same CMake helper.
    set(PRISM_SDK_ROOT ${CMAKE_CURRENT_LIST_DIR})
    include(${CMAKE_CURRENT_LIST_DIR}/cmake/PrismCartridge.cmake)
    prism_add_installable_cartridge(hello_cartridge
        ENTRY cartridge_hello
        SOURCES
            ${CMAKE_CURRENT_LIST_DIR}/examples/hello-cartridge/app.c
        INCLUDE_DIRECTORIES
            ${CMAKE_CURRENT_LIST_DIR}/examples/hello-cartridge
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/hello.prism
    )

    prism_add_installable_cartridge(runtime_imports_cartridge
        EXCLUDE_FROM_ALL
        ENTRY cartridge_runtime_imports
        SOURCES
            ${CMAKE_CURRENT_LIST_DIR}/tests/runtime_imports_cartridge.c
        INCLUDE_DIRECTORIES
            ${CMAKE_CURRENT_LIST_DIR}/tests
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/runtime-imports.prism
    )

    prism_add_installable_cartridge(bongocat_cartridge
        ENTRY cartridge_bongocat
        SOURCES
            ${CMAKE_CURRENT_LIST_DIR}/src/cartridges/bongocat/app.c
        INCLUDE_DIRECTORIES
            ${CMAKE_CURRENT_LIST_DIR}/src/cartridges/bongocat
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/bongocat.prism
    )

    prism_add_installable_cartridge(morse_cartridge
        ENTRY cartridge_morse
        SOURCES
            ${CMAKE_CURRENT_LIST_DIR}/src/cartridges/morse/app.c
        INCLUDE_DIRECTORIES
            ${CMAKE_CURRENT_LIST_DIR}/src/cartridges/morse
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/morse.prism
    )

    prism_add_installable_cartridge(asteroids_cartridge
        ENTRY cartridge_asteroids
        SOURCES
            ${CMAKE_CURRENT_LIST_DIR}/src/cartridges/asteroids/app.c
        INCLUDE_DIRECTORIES
            ${CMAKE_CURRENT_LIST_DIR}/src/cartridges/asteroids
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/asteroids.prism
    )

    prism_add_installable_cartridge(beatline_cartridge
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
    target_sources(prism PRIVATE ${DEFAULT_CARTRIDGE_ASM})
endif()
