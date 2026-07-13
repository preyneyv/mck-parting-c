cmake_minimum_required(VERSION 3.20)

project(prism_host_cartridge_packages C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT PRISM_SDK_ROOT OR NOT PRISM_PACKAGE_OUTPUT_DIR)
  message(FATAL_ERROR
    "PRISM_SDK_ROOT and PRISM_PACKAGE_OUTPUT_DIR are required")
endif()

include("${PRISM_SDK_ROOT}/cmake/PrismCartridge.cmake")

prism_add_installable_cartridge(bongocat_cartridge
  ENTRY cartridge_bongocat
  SOURCES "${PRISM_SDK_ROOT}/src/cartridges/bongocat/app.c"
  INCLUDE_DIRECTORIES "${PRISM_SDK_ROOT}/src/cartridges/bongocat"
  OUTPUT "${PRISM_PACKAGE_OUTPUT_DIR}/bongocat.prism"
)

prism_add_installable_cartridge(morse_cartridge
  ENTRY cartridge_morse
  SOURCES "${PRISM_SDK_ROOT}/src/cartridges/morse/app.c"
  INCLUDE_DIRECTORIES "${PRISM_SDK_ROOT}/src/cartridges/morse"
  OUTPUT "${PRISM_PACKAGE_OUTPUT_DIR}/morse.prism"
)

prism_add_installable_cartridge(asteroids_cartridge
  ENTRY cartridge_asteroids
  SOURCES "${PRISM_SDK_ROOT}/src/cartridges/asteroids/app.c"
  INCLUDE_DIRECTORIES "${PRISM_SDK_ROOT}/src/cartridges/asteroids"
  OUTPUT "${PRISM_PACKAGE_OUTPUT_DIR}/asteroids.prism"
)

prism_add_installable_cartridge(beatline_cartridge
  ENTRY cartridge_beatline
  SOURCES
    "${PRISM_SDK_ROOT}/src/cartridges/beatline/app.c"
    "${PRISM_SDK_ROOT}/src/cartridges/beatline/chart.c"
    "${PRISM_SDK_ROOT}/src/cartridges/beatline/game.c"
  INCLUDE_DIRECTORIES "${PRISM_SDK_ROOT}/src/cartridges/beatline"
  OUTPUT "${PRISM_PACKAGE_OUTPUT_DIR}/beatline.prism"
)

prism_add_installable_cartridge(state_lifecycle_cartridge
  ENTRY cartridge_state_lifecycle
  SOURCES "${PRISM_SDK_ROOT}/tests/state_lifecycle_cartridge.c"
  INCLUDE_DIRECTORIES "${PRISM_SDK_ROOT}/tests"
  OUTPUT "${PRISM_PACKAGE_OUTPUT_DIR}/state-lifecycle.prism"
)
