include(CMakeParseArguments)

# Build a native RP2040 cartridge and expose a .prism file as a normal CMake
# artifact.  Authors use `cmake --build build`; this helper is deliberately
# behind the build system rather than presented as a separate Prism CLI.
function(prism_add_installable_cartridge TARGET)
  set(options EXCLUDE_FROM_ALL)
  set(oneValueArgs ENTRY OUTPUT)
  set(multiValueArgs SOURCES INCLUDE_DIRECTORIES COMPILE_DEFINITIONS)
  cmake_parse_arguments(PRISM "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if(NOT PRISM_ENTRY OR NOT PRISM_SOURCES)
    message(FATAL_ERROR "prism_add_installable_cartridge requires ENTRY and SOURCES")
  endif()
  if(NOT DEFINED PRISM_SDK_ROOT)
    message(FATAL_ERROR "Set PRISM_SDK_ROOT to the Prism SDK checkout")
  endif()

  include("${PRISM_SDK_ROOT}/cmake/cartridge_boundary.cmake")
  set(PRISM_BOUNDARY_FILES ${PRISM_SOURCES})
  foreach(INCLUDE_DIRECTORY IN LISTS PRISM_INCLUDE_DIRECTORIES)
    file(GLOB_RECURSE PRISM_LOCAL_HEADERS CONFIGURE_DEPENDS
      "${INCLUDE_DIRECTORY}/*.h"
    )
    list(APPEND PRISM_BOUNDARY_FILES ${PRISM_LOCAL_HEADERS})
  endforeach()
  prism_check_cartridge_files(${PRISM_BOUNDARY_FILES})
  if(NOT PRISM_OUTPUT)
    set(PRISM_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${TARGET}.prism")
  endif()

  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  add_executable(${TARGET}_elf EXCLUDE_FROM_ALL
    ${PRISM_SOURCES}
    "${PRISM_SDK_ROOT}/src/prism/compiler_rt.c"
    "${PRISM_SDK_ROOT}/src/prism/compiler_rt.S"
    "${PRISM_SDK_ROOT}/src/shared/audio/song.c"
  )
  set_target_properties(${TARGET}_elf PROPERTIES SUFFIX ".elf")
  target_include_directories(${TARGET}_elf PRIVATE
    "${PRISM_SDK_ROOT}/src"
    "${PRISM_SDK_ROOT}/lib/u8g2/csrc"
    "${PRISM_SDK_ROOT}/lib/qrcodegen"
    ${PRISM_INCLUDE_DIRECTORIES}
  )
  target_compile_options(${TARGET}_elf PRIVATE
    -mcpu=cortex-m0plus -mthumb -Os -g0 -ffreestanding
    -ffunction-sections -fdata-sections
    -fPIC -mlong-calls -msingle-pic-base -mpic-register=r9
    -mno-pic-data-is-text-relative
    -Wall -Wextra -Wformat=2
    -Werror=implicit-function-declaration -Werror=return-type
  )
  target_compile_definitions(${TARGET}_elf PRIVATE
    NDEBUG=1
    ${PRISM_COMPILE_DEFINITIONS}
  )

  set(PRISM_IMPORT_MANIFEST "${PRISM_SDK_ROOT}/src/prism/imports.def")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${PRISM_IMPORT_MANIFEST}"
  )
  file(STRINGS "${PRISM_IMPORT_MANIFEST}" PRISM_IMPORT_ROWS)
  set(PRISM_IMPORT_DEFINITIONS)
  foreach(ROW IN LISTS PRISM_IMPORT_ROWS)
    if(ROW MATCHES "^[ ]*(//.*)?$")
      continue()
    endif()
    if(NOT ROW MATCHES
       "^PRISM_IMPORT\\((0x[0-9a-fA-F]+),[ ]*[A-Z0-9_]+,[ ]*([A-Za-z0-9_]+),")
      message(FATAL_ERROR "Invalid Prism import manifest row: ${ROW}")
    endif()
    set(IMPORT_ID "${CMAKE_MATCH_1}")
    set(LINKER_SYMBOL "${CMAKE_MATCH_2}")
    # These routines are linked into the ARM cartridge. Keeping comparator and
    # song hooks in guest code avoids crossing callback pointers through the
    # 32-bit ARM / 64-bit desktop ABI boundary.
    if(LINKER_SYMBOL STREQUAL "qsort" OR
       LINKER_SYMBOL MATCHES "^audio_song_player_")
      continue()
    endif()
    math(EXPR IMPORT_ADDRESS "0xf0000000 | ${IMPORT_ID}" OUTPUT_FORMAT HEXADECIMAL)
    list(APPEND PRISM_IMPORT_DEFINITIONS
      "${LINKER_SYMBOL}=${IMPORT_ADDRESS}"
    )
  endforeach()
  set(PRISM_LINK_OPTIONS
    -nostdlib
    "LINKER:--emit-relocs"
    "LINKER:--gc-sections"
    "LINKER:-e,${PRISM_ENTRY}"
    "LINKER:-T,${PRISM_SDK_ROOT}/cmake/prism_cartridge.ld"
  )
  foreach(DEFINITION IN LISTS PRISM_IMPORT_DEFINITIONS)
    list(APPEND PRISM_LINK_OPTIONS "LINKER:--defsym,${DEFINITION}")
  endforeach()
  target_link_options(${TARGET}_elf PRIVATE ${PRISM_LINK_OPTIONS})
  target_link_libraries(${TARGET}_elf PRIVATE gcc)
  set_property(TARGET ${TARGET}_elf APPEND PROPERTY LINK_DEPENDS
    "${PRISM_SDK_ROOT}/cmake/prism_cartridge.ld")

  add_custom_command(
    OUTPUT "${PRISM_OUTPUT}"
    COMMAND "${Python3_EXECUTABLE}"
            "${PRISM_SDK_ROOT}/scripts/package_cartridge.py"
            --elf "$<TARGET_FILE:${TARGET}_elf>"
            --output "${PRISM_OUTPUT}"
    DEPENDS ${TARGET}_elf "${PRISM_SDK_ROOT}/scripts/package_cartridge.py"
    VERBATIM
    COMMENT "Packaging ${TARGET}.prism"
  )
  if(PRISM_EXCLUDE_FROM_ALL)
    add_custom_target(${TARGET} DEPENDS "${PRISM_OUTPUT}")
  else()
    add_custom_target(${TARGET} ALL DEPENDS "${PRISM_OUTPUT}")
  endif()
  set(${TARGET}_PRISM_FILE "${PRISM_OUTPUT}" PARENT_SCOPE)
endfunction()
