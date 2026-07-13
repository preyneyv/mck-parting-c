#pragma once

#include <stdbool.h>

bool host_cartridge_init_bundled(void);
bool host_cartridge_load(const char *path);
bool host_cartridge_test_lifecycle(const char *path);
bool host_cartridge_test_audio(const char *path);
bool host_cartridge_test_assets(const char *path);
