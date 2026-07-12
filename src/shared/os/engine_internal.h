#pragma once

#include <stdbool.h>

bool engine_is_paused(void);
const char *engine_app_name(void);
void engine_finish_wake(void);
