# FindAseprite.cmake
#
# Provides:
#   ASEPRITE_EXECUTABLE
#   Aseprite_FOUND

set(ASEPRITE_EXECUTABLE "" CACHE FILEPATH "Path to Aseprite executable")

if(NOT ASEPRITE_EXECUTABLE)
    if(CMAKE_HOST_APPLE)
        # Native .app install
        find_program(_ASEPRITE_EXECUTABLE
            NAMES Aseprite aseprite
            PATHS
            "/Applications/Aseprite.app/Contents/MacOS"
            "$ENV{HOME}/Applications/Aseprite.app/Contents/MacOS"
            NO_DEFAULT_PATH
            NO_CMAKE_FIND_ROOT_PATH
        )

        # Steam default library path on macOS:
        # ~/Library/Application Support/Steam/steamapps/common/Aseprite/Aseprite.app/Contents/MacOS/Aseprite
        if(NOT _ASEPRITE_EXECUTABLE)
            find_program(_ASEPRITE_EXECUTABLE
                NAMES Aseprite aseprite
                PATHS "$ENV{HOME}/Library/Application Support/Steam/steamapps/common/Aseprite/Aseprite.app/Contents/MacOS"
                NO_DEFAULT_PATH
                NO_CMAKE_FIND_ROOT_PATH
            )
        endif()

        # Fallback: PATH search
        if(NOT _ASEPRITE_EXECUTABLE)
            find_program(_ASEPRITE_EXECUTABLE NAMES Aseprite aseprite NO_CMAKE_FIND_ROOT_PATH)
        endif()

    elseif(CMAKE_HOST_WIN32)
        # Try Steam install path from registry (common case)
        set(_STEAM_INSTALL "")
        get_filename_component(_STEAM_INSTALL
            "[HKEY_CURRENT_USER\\Software\\Valve\\Steam;SteamPath]"
            ABSOLUTE
        )
        if(NOT _STEAM_INSTALL OR _STEAM_INSTALL STREQUAL "/registry")
            get_filename_component(_STEAM_INSTALL
                "[HKEY_LOCAL_MACHINE\\SOFTWARE\\WOW6432Node\\Valve\\Steam;InstallPath]"
                ABSOLUTE
            )
        endif()

        set(_STEAM_HINTS "")
        if(_STEAM_INSTALL AND NOT _STEAM_INSTALL STREQUAL "/registry")
            list(APPEND _STEAM_HINTS "${_STEAM_INSTALL}")
        endif()
        # Safe hardcoded fallbacks (avoid $ENV{ProgramFiles(x86)} parsing issues)
        list(APPEND _STEAM_HINTS
            "C:/Program Files (x86)/Steam"
            "C:/Program Files/Steam"
            "$ENV{ProgramFiles}/Steam"
            "$ENV{ProgramW6432}/Steam"
        )

        find_program(_ASEPRITE_EXECUTABLE
            NAMES Aseprite.exe aseprite.exe
            HINTS ${_STEAM_HINTS}
            PATH_SUFFIXES "steamapps/common/Aseprite"
            NO_DEFAULT_PATH
            NO_CMAKE_FIND_ROOT_PATH
        )

        if(NOT _ASEPRITE_EXECUTABLE)
            find_program(_ASEPRITE_EXECUTABLE NAMES Aseprite.exe aseprite.exe NO_CMAKE_FIND_ROOT_PATH)
        endif()

    else()
        find_program(_ASEPRITE_EXECUTABLE NAMES aseprite Aseprite NO_CMAKE_FIND_ROOT_PATH)
    endif()

    if(_ASEPRITE_EXECUTABLE)
        set(ASEPRITE_EXECUTABLE "${_ASEPRITE_EXECUTABLE}" CACHE FILEPATH "Path to Aseprite executable" FORCE)
    endif()
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Aseprite REQUIRED_VARS ASEPRITE_EXECUTABLE)

mark_as_advanced(ASEPRITE_EXECUTABLE)
