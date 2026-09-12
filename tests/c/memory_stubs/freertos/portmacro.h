#pragma once
#include <pthread.h>
typedef pthread_mutex_t portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED PTHREAD_MUTEX_INITIALIZER
extern _Thread_local unsigned test_memory_lock_depth;
#define taskENTER_CRITICAL(lock) do { pthread_mutex_lock(lock); ++test_memory_lock_depth; } while (0)
#define taskEXIT_CRITICAL(lock) do { --test_memory_lock_depth; pthread_mutex_unlock(lock); } while (0)
