#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
  void *impl;
} platform_mutex_t;

typedef struct {
  void *impl;
} platform_queue_t;

void platform_mutex_init(platform_mutex_t *mutex);
void platform_mutex_lock(platform_mutex_t *mutex);
void platform_mutex_unlock(platform_mutex_t *mutex);

bool platform_queue_init(platform_queue_t *queue, size_t element_size,
                         size_t capacity);
bool platform_queue_try_add(platform_queue_t *queue, const void *element);
bool platform_queue_try_remove(platform_queue_t *queue, void *out_element);

void platform_memory_barrier();
