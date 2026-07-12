if(WIN32)
  set(ENV{PATH} "C:\\msys64\\usr\\bin;C:\\msys64\\mingw64\\bin;$ENV{PATH}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}"
          -S "${PRISM_SOURCE}"
          -B "${PRISM_BINARY}"
          -G "${PRISM_GENERATOR}"
          -DCMAKE_BUILD_TYPE=${PRISM_BUILD_TYPE}
          -DCMAKE_INSTALL_PREFIX=${PRISM_INSTALL}
          -DCMAKE_MAKE_PROGRAM=${PRISM_MAKE_PROGRAM}
          -DCMAKE_C_COMPILER=${PRISM_C_COMPILER}
          -DBUILD_SHARED_LIBS=OFF
          -DUNICORN_ARCH=arm
          -DUNICORN_BUILD_TESTS=OFF
          -DUNICORN_INSTALL=ON
          -DUNICORN_LEGACY_STATIC_ARCHIVE=OFF
  RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Failed to configure Unicorn (${result})")
endif()
