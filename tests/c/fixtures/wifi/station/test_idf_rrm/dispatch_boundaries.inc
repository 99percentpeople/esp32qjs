
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG -2
static int dispatch_mode, calls, queries, native_rc, native_ownership;
static void callback(void *id,const uint8_t *data,size_t n) {(void)id;(void)data;(void)n;}
static int identity;
static int esp32qjs_rrm_request(void (*cb)(void *,const uint8_t *,size_t),void *id,bool *attempted) {
    assert(cb==callback && id==&identity);calls++;*attempted=true;return native_rc;
}
static int esp32qjs_rrm_cancel(void (*cb)(void *,const uint8_t *,size_t),void *id) {
    assert(cb==callback && id==&identity);calls++;return native_rc;
}
static int esp32qjs_rrm_query(void (*cb)(void *,const uint8_t *,size_t),void *id) {
    assert(cb==callback && id==&identity);queries++;return native_ownership;
}
static int eloop_register_timeout_blocking(int (*fn)(void *,void *),void *data,void *ctx) {
    if(dispatch_mode==1)return -1;
    if(dispatch_mode==2)return 0; /* Even success without entry is not proof. */
    return fn(data,ctx);
}
