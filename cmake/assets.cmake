# Asset generation for apps
# - Alias src/shared/apps/<app>/... to build/shared/apps/<app>/... for includes
# - Convert .rpp files to .mid and .h files
# - Convert .aseprite files to .h files

find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_package(Reaper)
find_package(Aseprite)

# src/shared/apps/<app>/... can include build/shared/apps/<app>/...
foreach(SRC IN LISTS SHARED_SOURCES)
    string(REGEX MATCH "^src/shared/apps/([^/]+)/" _ "${SRC}")
    if(CMAKE_MATCH_1)
        set(APP_NAME "${CMAKE_MATCH_1}")
        set_source_files_properties(${SRC} PROPERTIES
            INCLUDE_DIRECTORIES "${CMAKE_CURRENT_BINARY_DIR}/shared/apps/${APP_NAME}"
        )
    endif()
endforeach()


# sound asset exports (.rpp -> .mid -> .h)
if(Reaper_FOUND)
    message(STATUS "REAPER found: ${REAPER_EXECUTABLE}")

    file(GLOB_RECURSE APP_RPP_FILES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/shared/apps/*/sounds/*.rpp"
    )
    set(GENERATED_SOUND_HEADERS "")

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
        set(OUT_BASE "${CMAKE_CURRENT_SOURCE_DIR}/src/shared/apps/${APP_NAME}/sounds/${SOUND_REL_NOEXT}")
        set(OUT_GEN_BASE "${CMAKE_CURRENT_BINARY_DIR}/shared/apps/${APP_NAME}/sounds/${SOUND_REL_NOEXT}")
        set(OUT_HEADER "${OUT_BASE}.h")
        set(OUT_MID "${OUT_GEN_BASE}.mid")

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
            --reaper-cli "${REAPER_EXECUTABLE}"
            DEPENDS "${RPP}"
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/rea_midi_export.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/rea_midi_export.lua"
            VERBATIM
            COMMENT "Generate sound header from ${RPP_REL_APPS}"
        )

        list(APPEND GENERATED_SOUND_HEADERS "${OUT_HEADER}")
    endforeach()

    add_custom_target(generate_sounds ALL DEPENDS ${GENERATED_SOUND_HEADERS})
    add_dependencies(shared generate_sounds)
else()
    message(WARNING "REAPER not found. Sound assets will not be generated.")
endif()


# sprite asset exports (.aseprite -> .h)
if(Aseprite_FOUND)
    message(STATUS "Aseprite found: ${ASEPRITE_EXECUTABLE}")

    file(GLOB_RECURSE APP_ASE_FILES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/shared/apps/*/sprites/*.aseprite"
    )
    set(GENERATED_SPRITE_HEADERS "")

    foreach(ASE IN LISTS APP_ASE_FILES)
        file(RELATIVE_PATH ASE_REL_APPS
            "${CMAKE_CURRENT_SOURCE_DIR}/src/shared/apps"
            "${ASE}"
        )
        string(REPLACE "\\" "/" ASE_REL_APPS "${ASE_REL_APPS}")
        # should give us <app>/sprites/<subdirs>/<name>.aseprite
        string(REGEX MATCH "^([^/]+)/sprites/(.+)\\.aseprite$" _ "${ASE_REL_APPS}")
        if(NOT CMAKE_MATCH_1)
            message(FATAL_ERROR "Unexpected aseprite path: ${ASE_REL_APPS}")
        endif()

        set(APP_NAME "${CMAKE_MATCH_1}")
        set(SPRITE_REL_NOEXT "${CMAKE_MATCH_2}")
        set(OUT_BASE "${CMAKE_CURRENT_SOURCE_DIR}/src/shared/apps/${APP_NAME}/sprites/${SPRITE_REL_NOEXT}")
        set(OUT_HEADER "${OUT_BASE}.h")

        add_custom_command(
            OUTPUT "${OUT_HEADER}"
            COMMAND ${Python3_EXECUTABLE}
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/aseprite_xbm_export.py"
            --aseprite "${ASE}"
            --header-out "${OUT_HEADER}"
            --aseprite-cli "${ASEPRITE_EXECUTABLE}"
            DEPENDS "${ASE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/aseprite_xbm_export.py"
            VERBATIM
            COMMENT "Generate sprite header from ${ASE_REL_APPS}"
        )

        list(APPEND GENERATED_SPRITE_HEADERS "${OUT_HEADER}")
    endforeach()

    add_custom_target(generate_sprites ALL DEPENDS ${GENERATED_SPRITE_HEADERS})
    add_dependencies(shared generate_sprites)
else()
    message(WARNING "Aseprite not found. Sprite assets will not be generated.")
endif()
