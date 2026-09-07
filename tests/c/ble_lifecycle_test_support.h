/* Minimal SDK/JS fixture types for compiled production lifecycle functions. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdatomic.h>
#include "esp32_mquickjs_wireless_core.h"
#define CONFIG_ESP32_MQUICKJS_BLE_MAX_CONNECTIONS 2
#define BLE_HS_EBUSY 15
#define BLE_HS_ENOMEM 6
#define BLE_HS_EAPP 9
#define BLE_HS_EUNKNOWN 1
#define BLE_HS_EDONE 14
#define taskENTER_CRITICAL(x) ((void)0)
#define taskEXIT_CRITICAL(x) ((void)0)
typedef struct esp32_mquickjs_future_driver_state {
    void *ctx, *runtime;
    int token, owner_ref, queue_ref;
    bool owner_rooted, queue_rooted, gatt_accounted, gatt_started;
    _Atomic bool detached, completed;
    uint32_t native_identity, connection_generation;
    uint16_t connection_index, callback_refs, mtu;
    int host_code;
    uint16_t max_descriptors, max_characteristics, max_services;
    uint16_t attribute_index, discover_service_index;
    bool include_descriptors;
} esp32_mquickjs_future_driver_state_t;
typedef struct { int uuid; uint16_t start_handle, end_handle, first_characteristic, characteristic_count; } ble_remote_service_t;
typedef struct { int uuid; uint16_t declaration_handle, value_handle, properties, first_descriptor, descriptor_count; } ble_remote_characteristic_t;
typedef struct { int uuid; uint16_t handle; } ble_remote_descriptor_t;
typedef struct {
    ble_remote_service_t *services;
    ble_remote_characteristic_t *characteristics;
    ble_remote_descriptor_t *descriptors;
    uint16_t conn_handle, service_count, characteristic_count, descriptor_count;
    _Atomic(esp32_mquickjs_future_driver_state_t *) active_gatt_state, active_gap_state;
    uint32_t generation;
    _Atomic unsigned gatt_pending;
    uint16_t mtu;
} ble_connection_slot_t;
static struct { int lock; unsigned max_connections; ble_connection_slot_t connections[2]; } s_ble = {.max_connections=2};
static _Atomic(esp32_mquickjs_future_driver_state_t *) s_ble_open_state, s_ble_server_notify_state;
static esp32_mquickjs_wireless_operation_registry_t s_ble_operations;
static unsigned deleted_roots, freed, wakes;
static void JS_DeleteGCRef(void *ctx, int *r) { (void)ctx; (void)r; deleted_roots++; }
static void ble_throw_error(void *ctx, const char *c, int h, int a, int i, int j) {
    (void)ctx; (void)c; (void)h; (void)a; (void)i; (void)j;
}
static int esp32_mquickjs_future_wake(void *runtime, int token) { (void)runtime; (void)token; wakes++; return 1; }
static void ble_future_state_storage_free(esp32_mquickjs_future_driver_state_t *state) { freed++; free(state); }
static void ble_native_state_complete(esp32_mquickjs_future_driver_state_t *state);
struct ble_gatt_error { int status; };

struct ble_gatt_dsc { int uuid; uint16_t handle; };
struct ble_gatt_chr { int uuid; uint16_t def_handle, val_handle, properties; };
struct ble_gatt_svc { int uuid; uint16_t start_handle, end_handle; };
static int ble_discover_descriptor_callback(uint16_t, const struct ble_gatt_error *, uint16_t, const struct ble_gatt_dsc *, void *);
static int ble_gattc_disc_all_dscs(uint16_t conn, uint16_t start, uint16_t end, int (*cb)(uint16_t,const struct ble_gatt_error *,uint16_t,const struct ble_gatt_dsc *,void *), void *arg) {
    (void)conn; (void)start; (void)end; (void)cb; (void)arg; return BLE_HS_EAPP;
}
static int ble_gattc_disc_all_chrs(uint16_t conn, uint16_t start, uint16_t end, int (*cb)(uint16_t,const struct ble_gatt_error *,const struct ble_gatt_chr *,void *), void *arg) {
    (void)conn; (void)start; (void)end; (void)cb; (void)arg; return BLE_HS_EAPP;
}
