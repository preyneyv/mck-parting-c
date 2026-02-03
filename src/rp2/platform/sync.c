#include <stdlib.h>
#include <string.h>

#include <hardware/sync.h>
#include <pico/sync.h>

#include <shared/platform/sync.h>

typedef struct {
  mutex_t mutex;
} platform_mutex_impl_t;

typedef struct {
  size_t element_size;
  size_t capacity;
  size_t head;
  size_t tail;
  size_t count;
  uint8_t *buffer;
  platform_mutex_t lock;
} platform_queue_impl_t;

void platform_mutex_init(platform_mutex_t *mutex) {
  platform_mutex_impl_t *impl = malloc(sizeof(platform_mutex_impl_t));
  mutex_init(&impl->mutex);
  mutex->impl = impl;
}

void platform_mutex_lock(platform_mutex_t *mutex) {
  platform_mutex_impl_t *impl = mutex->impl;
  mutex_enter_blocking(&impl->mutex);
}

void platform_mutex_unlock(platform_mutex_t *mutex) {
  platform_mutex_impl_t *impl = mutex->impl;
  mutex_exit(&impl->mutex);
}

bool platform_queue_init(platform_queue_t *queue, size_t element_size,
                         size_t capacity) {
  platform_queue_impl_t *impl = malloc(sizeof(platform_queue_impl_t));
  if (!impl) {
    return false;
  }
  impl->buffer = malloc(element_size * capacity);
  if (!impl->buffer) {
    free(impl);
    return false;
  }
  impl->element_size = element_size;
  impl->capacity = capacity;
  impl->head = 0;
  impl->tail = 0;
  impl->count = 0;
  platform_mutex_init(&impl->lock);
  queue->impl = impl;
  return true;
}

bool platform_queue_try_add(platform_queue_t *queue, const void *element) {
  platform_queue_impl_t *impl = queue->impl;
  platform_mutex_lock(&impl->lock);
  if (impl->count == impl->capacity) {
    platform_mutex_unlock(&impl->lock);
    return false;
  }
  memcpy(impl->buffer + (impl->head * impl->element_size), element,
         impl->element_size);
  impl->head = (impl->head + 1) % impl->capacity;
  impl->count++;
  platform_mutex_unlock(&impl->lock);
  return true;
}

bool platform_queue_try_remove(platform_queue_t *queue, void *out_element) {
  platform_queue_impl_t *impl = queue->impl;
  platform_mutex_lock(&impl->lock);
  if (impl->count == 0) {
    platform_mutex_unlock(&impl->lock);
    return false;
  }
  memcpy(out_element, impl->buffer + (impl->tail * impl->element_size),
         impl->element_size);
  impl->tail = (impl->tail + 1) % impl->capacity;
  impl->count--;
  platform_mutex_unlock(&impl->lock);
  return true;
}

void platform_memory_barrier() { __dmb(); }
