
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdatomic.h>
#define BLE_GAP_EVENT_NOTIFY_TX 1
#define BLE_HS_EDONE 14
#define CONFIG_ESP32_MQUICKJS_BLE_MAX_CONNECTIONS 2
#define taskENTER_CRITICAL(x) ((void)0)
#define taskEXIT_CRITICAL(x) ((void)0)
typedef struct esp32_mquickjs_future_driver_state {
    bool indication, notify_submitting, detached;
    uint16_t attribute_handle;
    uint16_t notify_handles[2];
    bool notify_pending[2];
    _Atomic uint16_t pending_confirmations;
    int host_code, token;
    void *runtime;
    _Atomic bool completed;
} esp32_mquickjs_future_driver_state_t;
static struct { int lock; } s_ble;
static _Atomic(esp32_mquickjs_future_driver_state_t *) s_ble_server_notify_state;
static int wakes;
static esp32_mquickjs_future_driver_state_t *ble_active_state_acquire(
    _Atomic(esp32_mquickjs_future_driver_state_t *) *p) { return atomic_load(p); }
static void ble_future_state_drop(esp32_mquickjs_future_driver_state_t *s) { (void)s; }
static void ble_native_state_complete(esp32_mquickjs_future_driver_state_t *s) { (void)s; atomic_store(&s_ble_server_notify_state, NULL); }
static int esp32_mquickjs_future_wake(void *r, int t) { (void)r; (void)t; wakes++; return 1; }
static void ble_future_wake_state(esp32_mquickjs_future_driver_state_t *s) { if (!s->detached) esp32_mquickjs_future_wake(s->runtime, s->token); }
struct event { int type; struct { bool indication; uint16_t attr_handle, conn_handle; int status; } notify_tx; };
static void callback(struct event *event) { switch (event->type) {
