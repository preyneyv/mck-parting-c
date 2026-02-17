# FindReaper.cmake
#
# Provides:
#   REAPER_EXECUTABLE
#   Reaper_FOUND

# Allow user override via cache
set(REAPER_EXECUTABLE "" CACHE FILEPATH "Path to REAPER executable")

if(NOT REAPER_EXECUTABLE)
    if(CMAKE_HOST_APPLE)
        # REAPER.app bundle binary is typically:
        # /Applications/REAPER.app/Contents/MacOS/REAPER (or reaper)
        find_program(_REAPER_EXECUTABLE
            NAMES REAPER reaper
            PATHS
            "/Applications/REAPER.app/Contents/MacOS"
            "$ENV{HOME}/Applications/REAPER.app/Contents/MacOS"
            NO_DEFAULT_PATH
            NO_CMAKE_FIND_ROOT_PATH
        )

        # Fallback: PATH search
        if(NOT _REAPER_EXECUTABLE)
            find_program(_REAPER_EXECUTABLE NAMES REAPER reaper NO_CMAKE_FIND_ROOT_PATH)
        endif()

    elseif(CMAKE_HOST_WIN32)
        find_program(_REAPER_EXECUTABLE
            NAMES reaper.exe
            PATHS
            "$ENV{ProgramFiles}/REAPER (x64)"
            "$ENV{ProgramFiles}/REAPER"
            "$ENV{ProgramW6432}/REAPER (x64)"
            "$ENV{ProgramW6432}/REAPER"
            "$ENV{LOCALAPPDATA}/Programs/REAPER (x64)"
            "$ENV{LOCALAPPDATA}/Programs/REAPER"
            NO_DEFAULT_PATH
            NO_CMAKE_FIND_ROOT_PATH
        )

        if(NOT _REAPER_EXECUTABLE)
            find_program(_REAPER_EXECUTABLE NAMES reaper.exe NO_CMAKE_FIND_ROOT_PATH)
        endif()
    else()
        find_program(_REAPER_EXECUTABLE NAMES reaper REAPER NO_CMAKE_FIND_ROOT_PATH)
    endif()

    if(_REAPER_EXECUTABLE)
        set(REAPER_EXECUTABLE "${_REAPER_EXECUTABLE}" CACHE FILEPATH "Path to REAPER executable" FORCE)
    endif()
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Reaper REQUIRED_VARS REAPER_EXECUTABLE)

mark_as_advanced(REAPER_EXECUTABLE)
