
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)

add_library(sdl2 INTERFACE)
target_include_directories(sdl2 INTERFACE ${SDL2_INCLUDE_DIRS})
target_link_libraries(sdl2 INTERFACE ${SDL2_LIBRARIES})
target_compile_options(sdl2 INTERFACE ${SDL2_CFLAGS_OTHER})
