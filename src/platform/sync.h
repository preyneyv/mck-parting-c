#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct platform_mutex_t platform_mutex_t;
typedef struct platform_queue_t platform_queue_t;

platform_mutex_t *platform_mutex_create(void);
void platform_mutex_lock(platform_mutex_t *mutex);
void platform_mutex_unlock(platform_mutex_t *mutex);

platform_queue_t *platform_queue_create(uint16_t element_size,
                                        uint16_t element_count);
bool platform_queue_try_push(platform_queue_t *queue, const void *item);
bool platform_queue_try_pop(platform_queue_t *queue, void *out);

void platform_memory_barrier(void);
