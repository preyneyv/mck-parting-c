#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include <platform/sync.h>

struct platform_mutex_t {
  pthread_mutex_t mutex;
};

struct platform_queue_t {
  pthread_mutex_t mutex;
  uint8_t *storage;
  uint16_t element_size;
  uint16_t element_count;
  uint16_t head;
  uint16_t tail;
  uint16_t count;
};

platform_mutex_t *platform_mutex_create(void) {
  platform_mutex_t *mutex = (platform_mutex_t *)calloc(1, sizeof(platform_mutex_t));
  if (mutex == NULL) {
    return NULL;
  }
  pthread_mutex_init(&mutex->mutex, NULL);
  return mutex;
}

void platform_mutex_lock(platform_mutex_t *mutex) {
  if (mutex == NULL) {
    return;
  }
  pthread_mutex_lock(&mutex->mutex);
}

void platform_mutex_unlock(platform_mutex_t *mutex) {
  if (mutex == NULL) {
    return;
  }
  pthread_mutex_unlock(&mutex->mutex);
}

platform_queue_t *platform_queue_create(uint16_t element_size,
                                        uint16_t element_count) {
  platform_queue_t *queue = (platform_queue_t *)calloc(1, sizeof(platform_queue_t));
  if (queue == NULL) {
    return NULL;
  }
  pthread_mutex_init(&queue->mutex, NULL);
  queue->storage = (uint8_t *)malloc((size_t)element_size * element_count);
  if (queue->storage == NULL) {
    return NULL;
  }
  queue->element_size = element_size;
  queue->element_count = element_count;
  return queue;
}

bool platform_queue_try_push(platform_queue_t *queue, const void *item) {
  if (queue == NULL || queue->storage == NULL) {
    return false;
  }
  pthread_mutex_lock(&queue->mutex);
  if (queue->count == queue->element_count) {
    pthread_mutex_unlock(&queue->mutex);
    return false;
  }
  memcpy(queue->storage + ((size_t)queue->tail * queue->element_size), item,
         queue->element_size);
  queue->tail = (uint16_t)((queue->tail + 1) % queue->element_count);
  queue->count++;
  pthread_mutex_unlock(&queue->mutex);
  return true;
}

bool platform_queue_try_pop(platform_queue_t *queue, void *out) {
  if (queue == NULL || queue->storage == NULL) {
    return false;
  }
  pthread_mutex_lock(&queue->mutex);
  if (queue->count == 0) {
    pthread_mutex_unlock(&queue->mutex);
    return false;
  }
  memcpy(out, queue->storage + ((size_t)queue->head * queue->element_size),
         queue->element_size);
  queue->head = (uint16_t)((queue->head + 1) % queue->element_count);
  queue->count--;
  pthread_mutex_unlock(&queue->mutex);
  return true;
}

void platform_memory_barrier(void) {}
