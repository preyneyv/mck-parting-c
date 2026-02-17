# Asset generation for apps
# - Alias generated headers to source paths for nice includes
# - Convert .rpp files to .mid and .h files
# - Convert .aseprite files to .h files

find_package(Python3 REQUIRED COMPONENTS Interpreter)

# src/shared/apps/<app>/... can include build/generated/shared/apps/<app>/...
foreach(SRC IN LISTS SHARED_SOURCES)
    string(REGEX MATCH "^src/shared/apps/([^/]+)/" _ "${SRC}")
    if(CMAKE_MATCH_1)
        set(APP_NAME "${CMAKE_MATCH_1}")
        set_source_files_properties(${SRC} PROPERTIES
            INCLUDE_DIRECTORIES "${CMAKE_CURRENT_BINARY_DIR}/generated/shared/apps/${APP_NAME}"
        )
    endif()
endforeach()


# convert REAPER project files to MIDI and header files.
file(GLOB_RECURSE APP_RPP_FILES CONFIGURE_DEPENDS 
    "${CMAKE_CURRENT_SOURCE_DIR}/src/shared/apps/*/sounds/*.rpp"
)
set(GENERATED_SOUND_HEADERS "")
set(GENERATED_SOUND_MIDIS "")

foreach(RPP IN LISTS APP_RPP_FILES)
    file(RELATIVE_PATH RPP_REL_APPS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/shared/apps"
        "${RPP}"
    )
    string(REPLACE "\\" "/" RPP_REL_APPS "${RPP_REL_APPS}")
    # should give us <app>/sounds/<subdirs>/<name>.rpp
    string(REGEX MATCH "^([^/]+)/sounds/(.+)\\.rpp$" _ "${RPP_REL_APPS}")
    if(NOT CMAKE_MATCH_1)
        message(FATAL_ERROR "Unexpected rpp path: ${RPP_REL_APPS}")
    endif()

    set(APP_NAME "${CMAKE_MATCH_1}")
    set(SOUND_REL_NOEXT "${CMAKE_MATCH_2}")
    set(OUT_BASE "${CMAKE_CURRENT_BINARY_DIR}/generated/shared/apps/${APP_NAME}/sounds/${SOUND_REL_NOEXT}")
    set(OUT_HEADER "${OUT_BASE}.h")
    set(OUT_MID "${OUT_BASE}.mid")

    # asteroids/sounds/bgm/wow -> bgm_wow
    set(SYMBOL_SRC "${SOUND_REL_NOEXT}")
    string(REGEX REPLACE "[^A-Za-z0-9_]" "_" SOUND_SYMBOL "${SYMBOL_SRC}")
    set(SOUND_SYMBOL "sound_${SOUND_SYMBOL}")

    get_filename_component(OUT_DIR "${OUT_BASE}" DIRECTORY)

    add_custom_command(
        OUTPUT "${OUT_HEADER}" "${OUT_MID}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${OUT_DIR}"
        COMMAND ${Python3_EXECUTABLE}
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/rea_midi_export.py"
            --rpp "${RPP}"
            --midi-out "${OUT_MID}"
            --header-out "${OUT_HEADER}"
            --symbol "${SOUND_SYMBOL}"
        DEPENDS "${RPP}"
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/rea_midi_export.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/rea_midi_export.lua"
        VERBATIM
        COMMENT "Generate MIDI and header from ${RPP_REL_APPS}"
    )

    list(APPEND GENERATED_SOUND_HEADERS "${OUT_HEADER}")
    list(APPEND GENERATED_SOUND_MIDIS "${OUT_MID}")
endforeach()

add_custom_target(generate_sounds ALL DEPENDS ${GENERATED_SOUND_HEADERS} ${GENERATED_SOUND_MIDIS})
add_dependencies(shared generate_sounds)
