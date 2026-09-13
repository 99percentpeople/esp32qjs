
#include <pthread.h>
typedef pthread_mutex_t portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED PTHREAD_MUTEX_INITIALIZER
static _Thread_local unsigned critical_depth;
#define portENTER_CRITICAL(lock) do { assert(!pthread_mutex_lock(lock));++critical_depth; } while(0)
#define portEXIT_CRITICAL(lock) do { --critical_depth;assert(!pthread_mutex_unlock(lock)); } while(0)
