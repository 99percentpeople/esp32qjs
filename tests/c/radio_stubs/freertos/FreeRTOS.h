#pragma once
#include <pthread.h>
#include <assert.h>
typedef pthread_mutex_t portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED PTHREAD_MUTEX_INITIALIZER
extern _Thread_local int test_critical_depth;
#define taskENTER_CRITICAL(lock) do { pthread_mutex_lock(lock); test_critical_depth++; } while (0)
#define taskEXIT_CRITICAL(lock) do { test_critical_depth--; pthread_mutex_unlock(lock); } while (0)
