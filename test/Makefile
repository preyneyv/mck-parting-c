# Makefile for macOS audio test
CC = clang
CFLAGS = -Wall -Wextra -std=c99 -O2
FRAMEWORKS = -framework AudioToolbox -framework CoreAudio
TARGETS = synth_example rp2040_port_example
AUDIO_LIB = audio_lib.o

# Default target
all: $(TARGETS)

# Build audio library object
$(AUDIO_LIB): audio.c audio_test.h
	$(CC) $(CFLAGS) -c -o $(AUDIO_LIB) audio.c

# Build the synthesis example
synth_example: synth_example.c $(AUDIO_LIB) audio_test.h
	$(CC) $(CFLAGS) $(FRAMEWORKS) -o synth_example synth_example.c $(AUDIO_LIB)

# Build the RP2040 port example
rp2040_port_example: rp2040_port_example.c $(AUDIO_LIB) audio_test.h
	$(CC) $(CFLAGS) $(FRAMEWORKS) -o rp2040_port_example rp2040_port_example.c $(AUDIO_LIB)

# Clean build artifacts
clean:
	rm -f $(TARGETS) $(AUDIO_LIB)

# Install dependencies (none needed for this simple test)
install:
	@echo "No external dependencies needed for this test"

# Run the synthesis example
run: synth_example
	./synth_example

# Run the RP2040 port example
run-rp2040: rp2040_port_example
	./rp2040_port_example

.PHONY: all clean install run run-rp2040
