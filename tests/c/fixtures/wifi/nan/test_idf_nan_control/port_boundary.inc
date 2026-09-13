
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>
typedef pthread_mutex_t portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED PTHREAD_MUTEX_INITIALIZER
static _Thread_local unsigned observer_depth;
uint32_t esp32_mquickjs_wifi_nan_tx_callback_enter(void *p){(void)p;return 0;}
bool esp32_mquickjs_wifi_nan_tx_callback_allowed(uint32_t id){(void)id;return true;}
void esp32_mquickjs_wifi_nan_tx_callback_leave(uint32_t id,bool success){(void)id;(void)success;}
#define portENTER_CRITICAL(p) do{assert(!observer_depth);assert(!pthread_mutex_lock(p));++observer_depth;}while(0)
#define portEXIT_CRITICAL(p) do{assert(observer_depth==1);--observer_depth;assert(!pthread_mutex_unlock(p));}while(0)
