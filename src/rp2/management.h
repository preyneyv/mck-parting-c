#pragma once

#include <stdbool.h>

void management_init(void);
void management_task(void);
/* Service quiet WebUSB traffic while USB-powered sleep is blocking the engine.
 * Returns true only when a deliberate management command should wake Prism. */
bool management_sleep_task(void);
