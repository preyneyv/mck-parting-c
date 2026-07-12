cmake_minimum_required(VERSION 3.20)

project(hello_cartridge C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(PRISM_SDK_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." CACHE PATH
  "Path to the Prism SDK checkout"
)
get_filename_component(PRISM_SDK_ROOT "${PRISM_SDK_ROOT}" ABSOLUTE)

include("${PRISM_SDK_ROOT}/cmake/cartridge_boundary.cmake")
prism_check_cartridge_boundary("${CMAKE_CURRENT_LIST_DIR}")

if(HELLO_PACKAGE_ONLY)
  include("${PRISM_SDK_ROOT}/cmake/PrismCartridge.cmake")
  prism_add_installable_cartridge(hello_cartridge
    ENTRY cartridge_hello
    SOURCES "${CMAKE_CURRENT_LIST_DIR}/app.c"
    INCLUDE_DIRECTORIES "${CMAKE_CURRENT_LIST_DIR}"
    OUTPUT "${CMAKE_BINARY_DIR}/hello.prism"
  )
  return()
endif()

find_package(SDL2 CONFIG REQUIRED)
if(NOT TARGET SDL2::SDL2-static)
  message(FATAL_ERROR "The Prism simulator requires the static SDL2 target")
endif()

add_subdirectory("${PRISM_SDK_ROOT}/lib/u8g2" "${CMAKE_BINARY_DIR}/u8g2")
add_subdirectory("${PRISM_SDK_ROOT}/lib/qrcodegen" "${CMAKE_BINARY_DIR}/qrcodegen")

add_executable(hello-simulator app.c simulator.c)
target_compile_definitions(hello-simulator PRIVATE PICO_ON_DEVICE=0)
target_include_directories(hello-simulator PRIVATE
  "${CMAKE_CURRENT_LIST_DIR}"
  "${PRISM_SDK_ROOT}/src"
)
target_link_libraries(hello-simulator PRIVATE
  u8g2
  qrcodegen
  SDL2::SDL2-static
)
if(NOT WIN32)
  target_link_libraries(hello-simulator PRIVATE m)
endif()

find_program(PRISM_ARM_GCC arm-none-eabi-gcc
  HINTS
    "$ENV{PICO_TOOLCHAIN_PATH}/bin"
    "$ENV{USERPROFILE}/.pico-sdk/toolchain/14_2_Rel1/bin"
    "$ENV{HOME}/.pico-sdk/toolchain/14_2_Rel1/bin"
)
if(NOT PRISM_ARM_GCC)
  message(FATAL_ERROR
    "arm-none-eabi-gcc is required to produce hello.prism; add it to PATH "
    "or set PRISM_ARM_GCC"
  )
endif()

include(ExternalProject)
ExternalProject_Add(hello-package
  SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}"
  BINARY_DIR "${CMAKE_BINARY_DIR}/package"
  CMAKE_ARGS
    "-DHELLO_PACKAGE_ONLY=ON"
    "-DPRISM_SDK_ROOT=${PRISM_SDK_ROOT}"
    "-DCMAKE_BUILD_TYPE=MinSizeRel"
    "-DCMAKE_C_COMPILER=${PRISM_ARM_GCC}"
    "-DCMAKE_SYSTEM_NAME=Generic"
    "-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY"
  BUILD_COMMAND "${CMAKE_COMMAND}" --build <BINARY_DIR>
  INSTALL_COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "<BINARY_DIR>/hello.prism" "${CMAKE_BINARY_DIR}/hello.prism"
  BUILD_BYPRODUCTS "${CMAKE_BINARY_DIR}/hello.prism"
)
