#!/bin/bash

# fix cwd
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
cd "$SCRIPT_DIR"
cd ..

# create build directory for device
mkdir build
cd build
cmake -GNinja -DPICO_BOARD=none ..

# create build directory for host
cd ..
mkdir build_host
cd build_host
cmake -GNinja -DPICO_BOARD=none -DPICO_PLATFORM=host ..
