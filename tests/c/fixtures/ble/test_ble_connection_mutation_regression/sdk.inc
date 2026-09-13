
#define _GNU_SOURCE
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#include <pthread.h>
#include <unistd.h>
#include <errno.h>
#include <sched.h>
#define BLE_HS_ENOTCONN 2
#define BLE_ERR_REM_USER_CONN_TERM 3
#define BLE_INVALID_CONN_HANDLE UINT16_MAX
#define portMAX_DELAY 0
static pthread_mutex_t mutex;
static pthread_mutex_t *s_ble_connection_mutex=&mutex;
static int xSemaphoreTakeRecursive(pthread_mutex_t *m,int wait) { (void)wait;return pthread_mutex_lock(m)==0; }
static int xSemaphoreGiveRecursive(pthread_mutex_t *m) { return pthread_mutex_unlock(m)==0; }
typedef struct { bool open,allocated,reserved,release_on_disconnect; unsigned generation; int conn_handle,connect_operation; } ble_connection_slot_t;
static struct { atomic_int rejected_connection_error; } s_ble;
static ble_connection_slot_t slot={.open=true,.allocated=true,.generation=1,.conn_handle=7};
static atomic_bool callback_waiting,callback_attempted,callback_done;
static bool contended,blocked_attempt;
static int calls,driver_result=BLE_HS_ENOTCONN;
static bool esp32_mquickjs_wireless_native_operation_complete(int *s) { *s=0;return true; }
static int ble_gap_terminate(int h,int reason) {
    (void)reason;assert(h==7);calls++;
    if(contended) {
        callback_waiting=true;
        /* Callback attempts the same framework lock while native I/O runs. */
        while(!callback_attempted) sched_yield();
    }
    return driver_result;
}
static void *host(void *arg) {
    (void)arg;
    while(!callback_waiting) usleep(1000);
    int rc=pthread_mutex_trylock(s_ble_connection_mutex);
    assert(rc==0 || rc==EBUSY);
    if(rc==EBUSY) {
        blocked_attempt=true;
        callback_attempted=true;
        xSemaphoreTakeRecursive(s_ble_connection_mutex,0);
    }
    /* SDK boundary: disconnect then another native connection with reused handle. */
    slot=(ble_connection_slot_t){.open=true,.allocated=true,.generation=2,.conn_handle=7};
    callback_done=true;
    callback_attempted=true;
    xSemaphoreGiveRecursive(s_ble_connection_mutex);
    return NULL;
}
static void init(void) {
    pthread_mutexattr_t attr;assert(!pthread_mutexattr_init(&attr));
    assert(!pthread_mutexattr_settype(&attr,PTHREAD_MUTEX_RECURSIVE));
    assert(!pthread_mutex_init(&mutex,&attr));pthread_mutexattr_destroy(&attr);
}
