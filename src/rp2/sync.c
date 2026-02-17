#include <stdlib.h>

#include <hardware/sync.h>
#include <pico/sync.h>
#include <pico/util/queue.h>

#include <platform/sync.h>

struct platform_mutex_t {
  mutex_t mutex;
};

struct platform_queue_t {
  queue_t queue;
};

platform_mutex_t *platform_mutex_create(void) {
  platform_mutex_t *mutex = (platform_mutex_t *)calloc(1, sizeof(platform_mutex_t));
  if (mutex == NULL) {
    return NULL;
  }
  mutex_init(&mutex->mutex);
  return mutex;
}

void platform_mutex_lock(platform_mutex_t *mutex) {
  if (mutex == NULL) {
    return;
  }
  mutex_enter_blocking(&mutex->mutex);
}

void platform_mutex_unlock(platform_mutex_t *mutex) {
  if (mutex == NULL) {
    return;
  }
  mutex_exit(&mutex->mutex);
}

platform_queue_t *platform_queue_create(uint16_t element_size,
                                        uint16_t element_count) {
  platform_queue_t *queue = (platform_queue_t *)calloc(1, sizeof(platform_queue_t));
  if (queue == NULL) {
    return NULL;
  }
  queue_init(&queue->queue, element_size, element_count);
  return queue;
}

bool platform_queue_try_push(platform_queue_t *queue, const void *item) {
  if (queue == NULL) {
    return false;
  }
  return queue_try_add(&queue->queue, item);
}

bool platform_queue_try_pop(platform_queue_t *queue, void *out) {
  if (queue == NULL) {
    return false;
  }
  return queue_try_remove(&queue->queue, out);
}

void platform_memory_barrier(void) { __dmb(); }
