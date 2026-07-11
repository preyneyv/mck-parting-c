include(CMakeParseArguments)

# Build a native RP2040 cartridge and expose a .prism file as a normal CMake
# artifact.  Authors use `cmake --build build`; this helper is deliberately
# behind the build system rather than presented as a separate Prism CLI.
function(prism_add_installable_cartridge TARGET)
  set(options)
  set(oneValueArgs UUID ENTRY OUTPUT)
  set(multiValueArgs SOURCES INCLUDE_DIRECTORIES COMPILE_DEFINITIONS)
  cmake_parse_arguments(PRISM "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if(NOT PRISM_UUID OR NOT PRISM_ENTRY OR NOT PRISM_SOURCES)
    message(FATAL_ERROR "prism_add_installable_cartridge requires UUID, ENTRY, and SOURCES")
  endif()
  if(NOT DEFINED PRISM_SDK_ROOT)
    message(FATAL_ERROR "Set PRISM_SDK_ROOT to the Prism SDK checkout")
  endif()
  if(NOT PRISM_OUTPUT)
    set(PRISM_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${TARGET}.prism")
  endif()

  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  add_executable(${TARGET}_elf EXCLUDE_FROM_ALL
    ${PRISM_SOURCES}
    "${PRISM_SDK_ROOT}/src/prism/compiler_rt.c"
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
  )
  target_compile_definitions(${TARGET}_elf PRIVATE ${PRISM_COMPILE_DEFINITIONS})

  set(PRISM_IMPORT_DEFINITIONS
    "u8g2_SetDrawColor=0xF0000001"
    "u8g2_SetFont=0xF0000002"
    "u8g2_DrawStr=0xF0000003"
    "snprintf=0xF0000004"
    "u8g2_GetStrWidth=0xF0000005"
    "u8g2_font_6x10_tf=0xF0000006"
    "u8g2_DrawXBM=0xF0000007"
    "u8g2_DrawBox=0xF0000008"
    "u8g2_DrawFrame=0xF0000009"
    "u8g2_DrawRBox=0xF000000A"
    "u8g2_DrawRFrame=0xF000000B"
    "u8g2_DrawHLine=0xF000000C"
    "u8g2_DrawVLine=0xF000000D"
    "u8g2_DrawPixel=0xF000000E"
    "u8g2_DrawLine=0xF000000F"
    "u8g2_DrawCircle=0xF0000010"
    "u8g2_DrawDisc=0xF0000011"
    "u8g2_DrawEllipse=0xF0000012"
    "u8g2_DrawFilledEllipse=0xF0000013"
    "u8g2_DrawTriangle=0xF0000014"
    "u8g2_DrawArc=0xF0000015"
    "u8g2_DrawUTF8=0xF0000016"
    "u8g2_SetBitmapMode=0xF0000017"
    "u8g2_font_4x6_tf=0xF0000018"
    "u8g2_font_5x7_mr=0xF0000019"
    "u8g2_font_5x7_tf=0xF000001A"
    "u8g2_font_5x7_tr=0xF000001B"
    "u8g2_font_7x14_mr=0xF000001C"
    "u8g2_font_7x14B_mr=0xF000001D"
    "u8g2_font_u8glib_4_tf=0xF000001E"
    "memcpy=0xF000001F"
    "memset=0xF0000020"
    "memmove=0xF0000021"
    "memcmp=0xF0000022"
    "strlen=0xF0000023"
    "strcmp=0xF0000024"
    "strncpy=0xF0000025"
    "malloc=0xF0000026"
    "calloc=0xF0000027"
    "realloc=0xF0000028"
    "free=0xF0000029"
    "sinf=0xF000002A"
    "cosf=0xF000002B"
    "sqrtf=0xF000002C"
    "fmodf=0xF000002D"
    "rand=0xF000002E"
    "srand=0xF000002F"
    "qrcodegen_getSize=0xF0000030"
    "qrcodegen_getModule=0xF0000031"
    "floorf=0xF0000032"
    "qsort=0xF0000033"
    "audio_song_player_init=0xF0000034"
    "audio_song_player_set_hook=0xF0000035"
    "audio_song_player_play=0xF0000036"
    "audio_song_player_stop=0xF0000037"
    "audio_song_player_pause=0xF0000038"
    "audio_song_player_resume=0xF0000039"
    "audio_song_player_tick=0xF000003A"
    "prism_import_aeabi_fdiv=0xF000003B"
    "strcat=0xF000003C"
    "strncmp=0xF000003D"
    "audio_synth_patch_config_set=0xF000003E"
    "audio_synth_enqueue=0xF000003F"
    "audio_song_player_seek=0xF0000040"
    "expf=0xF0000041"
    "abs=0xF0000042"
    "fabsf=0xF0000043"
    "atan2f=0xF0000044"
    "fmaxf=0xF0000045"
  )
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
            --uuid "${PRISM_UUID}"
    DEPENDS ${TARGET}_elf "${PRISM_SDK_ROOT}/scripts/package_cartridge.py"
    VERBATIM
    COMMENT "Packaging ${TARGET}.prism"
  )
  add_custom_target(${TARGET} ALL DEPENDS "${PRISM_OUTPUT}")
  set(${TARGET}_PRISM_FILE "${PRISM_OUTPUT}" PARENT_SCOPE)
endfunction()
