#!/bin/bash

# Quick test script for macOS audio synthesis testing

echo "macOS Audio Test for RP2040 Synthesis"
echo "======================================"

# Build if needed
if [ ! -f "synth_example" ] || [ ! -f "rp2040_port_example" ]; then
    echo "Building test programs..."
    make clean && make all
    if [ $? -ne 0 ]; then
        echo "Build failed!"
        exit 1
    fi
    echo "Build complete!"
    echo
fi

echo "Available test programs:"
echo "1. synth_example - Simple interactive synthesis test"
echo "2. rp2040_port_example - RP2040 code port with lookup tables"
echo "3. Build only"
echo "4. Clean build artifacts"
echo

read -p "Select option (1-4): " choice

case $choice in
    1)
        echo "Starting synth_example..."
        echo "Use keys 1-9 to change frequency, 'q' to quit"
        ./synth_example
        ;;
    2)
        echo "Starting rp2040_port_example..."
        echo "Use keys 1-9 for oscillator freq, a-j for LFO freq, 's' for status, 'q' to quit"
        ./rp2040_port_example
        ;;
    3)
        echo "Building..."
        make all
        ;;
    4)
        echo "Cleaning..."
        make clean
        ;;
    *)
        echo "Invalid option"
        exit 1
        ;;
esac
