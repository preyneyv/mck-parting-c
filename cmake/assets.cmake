# Asset generation for cartridges
# - Alias src/cartridges/<name>/... to build/cartridges/<name>/... for includes
# - Convert .rpp files to .mid and .h files
# - Convert .aseprite files to .h files

find_package(Python3 REQUIRED COMPONENTS Interpreter)
option(PRISM_ASSET_AUTHORING "Enable REAPER/Aseprite asset export targets" OFF)

# Host builds consume the generated headers committed alongside their source
# assets. Launching GUI authoring tools during a simulator build is both
# unnecessary and unreliable (especially REAPER's single-instance automation).
if(PRISM_ASSET_AUTHORING AND NOT PICO_PLATFORM STREQUAL "host")
    find_package(Reaper)
    find_package(Aseprite)
endif()

# src/cartridges/<name>/... can include build/cartridges/<name>/...
foreach(SRC IN LISTS CARTRIDGE_SOURCES)
    string(REGEX MATCH "^src/cartridges/([^/]+)/" _ "${SRC}")
    if(CMAKE_MATCH_1)
        set(APP_NAME "${CMAKE_MATCH_1}")
        set_source_files_properties(${SRC} PROPERTIES
            INCLUDE_DIRECTORIES "${CMAKE_CURRENT_BINARY_DIR}/cartridges/${APP_NAME}"
        )
    endif()
endforeach()


# sound asset exports (.rpp -> .mid -> song .h)
if(PRISM_ASSET_AUTHORING AND Reaper_FOUND)
    message(STATUS "REAPER found: ${REAPER_EXECUTABLE}")
    # REAPER is effectively single-instance. Parallel exports can deadlock as
    # several CLI processes contend for the same GUI instance.
    set_property(GLOBAL APPEND PROPERTY JOB_POOLS reaper_export_pool=1)

    file(GLOB_RECURSE APP_RPP_FILES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/cartridges/*/sounds/*.rpp"
    )
    set(GENERATED_SOUND_HEADERS "")

    foreach(RPP IN LISTS APP_RPP_FILES)
        file(RELATIVE_PATH RPP_REL_APPS
            "${CMAKE_CURRENT_SOURCE_DIR}/src/cartridges"
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
        set(OUT_BASE "${CMAKE_CURRENT_SOURCE_DIR}/src/cartridges/${APP_NAME}/sounds/${SOUND_REL_NOEXT}")
        set(OUT_GEN_BASE "${CMAKE_CURRENT_BINARY_DIR}/cartridges/${APP_NAME}/sounds/${SOUND_REL_NOEXT}")
        set(OUT_HEADER "${OUT_BASE}.h")
        set(OUT_MID "${OUT_GEN_BASE}.mid")

        # asteroids/sounds/bgm/wow -> bgm_wow
        set(SYMBOL_SRC "${SOUND_REL_NOEXT}")
        string(REGEX REPLACE "[^A-Za-z0-9_]" "_" SOUND_SYMBOL "${SYMBOL_SRC}")
        set(SOUND_SYMBOL "sound_${SOUND_SYMBOL}")

        get_filename_component(OUT_DIR "${OUT_BASE}" DIRECTORY)

        if(APP_NAME STREQUAL "beatline" AND
           (SOUND_REL_NOEXT STREQUAL "golden" OR
            SOUND_REL_NOEXT STREQUAL "never_gonna"))
            if(SOUND_REL_NOEXT STREQUAL "golden")
                set(TRACK_TITLE "Golden")
                set(TRACK_ARTIST "HUNTR/X")
            else()
                set(TRACK_TITLE "Never Gonna")
                set(TRACK_ARTIST "Rick Astley")
            endif()
            set(OUT_TRACK
                "${CMAKE_CURRENT_SOURCE_DIR}/assets/beatline/${SOUND_REL_NOEXT}.beatline")
            get_filename_component(TRACK_OUT_DIR "${OUT_TRACK}" DIRECTORY)
            add_custom_command(
                OUTPUT "${OUT_TRACK}"
                COMMAND ${CMAKE_COMMAND} -E make_directory "${TRACK_OUT_DIR}"
                COMMAND ${Python3_EXECUTABLE}
                "${CMAKE_CURRENT_SOURCE_DIR}/scripts/rea_midi_export.py"
                --rpp "${RPP}"
                --midi-out "${OUT_MID}"
                --beatline-out "${OUT_TRACK}"
                --title "${TRACK_TITLE}"
                --artist "${TRACK_ARTIST}"
                --reaper-cli "${REAPER_EXECUTABLE}"
                DEPENDS "${RPP}"
                "${CMAKE_CURRENT_SOURCE_DIR}/scripts/rea_midi_export.py"
                "${CMAKE_CURRENT_SOURCE_DIR}/scripts/rea_midi_export.lua"
                VERBATIM
                JOB_POOL reaper_export_pool
                COMMENT "Generate Beatline track from ${RPP_REL_APPS}"
            )
            list(APPEND GENERATED_SOUND_HEADERS "${OUT_TRACK}")
        else()
            add_custom_command(
                OUTPUT "${OUT_HEADER}"
                COMMAND ${CMAKE_COMMAND} -E make_directory "${OUT_DIR}"
                COMMAND ${Python3_EXECUTABLE}
                "${CMAKE_CURRENT_SOURCE_DIR}/scripts/rea_midi_export.py"
                --rpp "${RPP}"
                --midi-out "${OUT_MID}"
                --song-header-out "${OUT_HEADER}"
                --symbol "${SOUND_SYMBOL}"
                --reaper-cli "${REAPER_EXECUTABLE}"
                DEPENDS "${RPP}"
                "${CMAKE_CURRENT_SOURCE_DIR}/scripts/rea_midi_export.py"
                "${CMAKE_CURRENT_SOURCE_DIR}/scripts/rea_midi_export.lua"
                VERBATIM
                JOB_POOL reaper_export_pool
                COMMENT "Generate sound header from ${RPP_REL_APPS}"
            )
            list(APPEND GENERATED_SOUND_HEADERS "${OUT_HEADER}")
        endif()
    endforeach()

    # Generated headers are committed. Regeneration is an explicit authoring
    # action so ordinary firmware builds never launch REAPER unexpectedly.
    add_custom_target(generate_sounds DEPENDS ${GENERATED_SOUND_HEADERS})
elseif(PRISM_ASSET_AUTHORING AND NOT PICO_PLATFORM STREQUAL "host")
    message(WARNING "REAPER not found. Sound assets will not be generated.")
endif()


# sprite asset exports (.aseprite -> .h)
if(PRISM_ASSET_AUTHORING AND Aseprite_FOUND)
    message(STATUS "Aseprite found: ${ASEPRITE_EXECUTABLE}")

    file(GLOB_RECURSE APP_ASE_FILES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/cartridges/*/sprites/*.aseprite"
    )
    set(GENERATED_SPRITE_HEADERS "")

    foreach(ASE IN LISTS APP_ASE_FILES)
        file(RELATIVE_PATH ASE_REL_APPS
            "${CMAKE_CURRENT_SOURCE_DIR}/src/cartridges"
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
        set(OUT_BASE "${CMAKE_CURRENT_SOURCE_DIR}/src/cartridges/${APP_NAME}/sprites/${SPRITE_REL_NOEXT}")
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

    set(PRISMPACK_ASE
        "${CMAKE_CURRENT_SOURCE_DIR}/src/shared/os/sprites/prismpack.aseprite")
    set(PRISMPACK_HEADER
        "${CMAKE_CURRENT_SOURCE_DIR}/src/shared/os/sprites/prismpack.h")
    set(PRISMPACK_TYPESCRIPT
        "${CMAKE_CURRENT_SOURCE_DIR}/web/lib/prismpack-icon.ts")
    if(EXISTS "${PRISMPACK_ASE}")
        add_custom_command(
            OUTPUT "${PRISMPACK_HEADER}" "${PRISMPACK_TYPESCRIPT}"
            COMMAND ${Python3_EXECUTABLE}
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/aseprite_xbm_export.py"
            --aseprite "${PRISMPACK_ASE}"
            --header-out "${PRISMPACK_HEADER}"
            --typescript-out "${PRISMPACK_TYPESCRIPT}"
            --typescript-name prismpackIcon
            --aseprite-cli "${ASEPRITE_EXECUTABLE}"
            DEPENDS "${PRISMPACK_ASE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/aseprite_xbm_export.py"
            VERBATIM
            COMMENT "Generate Prism asset-pack icon"
        )
        list(APPEND GENERATED_SPRITE_HEADERS
            "${PRISMPACK_HEADER}" "${PRISMPACK_TYPESCRIPT}")
    endif()

    add_custom_target(generate_sprites DEPENDS ${GENERATED_SPRITE_HEADERS})
elseif(PRISM_ASSET_AUTHORING AND NOT PICO_PLATFORM STREQUAL "host")
    message(WARNING "Aseprite not found. Sprite assets will not be generated.")
endif()
