
#include <assert.h>
#include <stddef.h>
typedef int esp_err_t;
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_STATE 2
static int s_phy_access_lock,awake,called;
static void _lock_acquire(int *lock){assert(!*lock);*lock=1;}
static void _lock_release(int *lock){assert(*lock);*lock=0;}
static int phy_get_modem_flag(void){assert(s_phy_access_lock);return awake;}
static int apply(void *p){assert(s_phy_access_lock);called++;return *(int*)p;}
