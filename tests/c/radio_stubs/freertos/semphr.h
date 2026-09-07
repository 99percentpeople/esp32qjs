#pragma once
#include <pthread.h>
#include <errno.h>
#include <assert.h>
void radio_test_mutex_contended(void);
typedef pthread_mutex_t StaticSemaphore_t;
typedef pthread_mutex_t *SemaphoreHandle_t;
#define portMAX_DELAY -1
static inline SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t *s) { pthread_mutex_init(s, NULL); return s; }
static inline int xSemaphoreTake(SemaphoreHandle_t s, int delay) {
    (void)delay;
    int result = pthread_mutex_trylock(s);
    if (result == 0) return 1;
    assert(result == EBUSY);
    radio_test_mutex_contended();
    return pthread_mutex_lock(s) == 0;
}
static inline int xSemaphoreGive(SemaphoreHandle_t s) { return pthread_mutex_unlock(s) == 0; }
