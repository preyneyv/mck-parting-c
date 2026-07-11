#pragma once

#include <prism/cartridge.h>

bool prism_cartridge_launch(const prism_cartridge_t *cartridge);
const prism_cartridge_t *prism_cartridge_current(void);
const prism_api_v1_t *prism_os_api(void);
void prism_cartridge_persistence_task(void);
void prism_cartridge_persistence_flush(void);
void prism_cartridge_persistence_set_deferred(bool deferred);
