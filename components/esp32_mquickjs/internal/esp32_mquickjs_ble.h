#pragma once

#include "sdkconfig.h"
#include "esp32_mquickjs_types.h"

#if CONFIG_ESP32_MQUICKJS_FEATURE_BLE

bool esp32_mquickjs_init_ble_runtime(JSContext *ctx,
                                     esp32_mquickjs_runtime_t *runtime);
void esp32_mquickjs_deinit_ble_runtime(JSContext *ctx);

JSValue js_ble_capabilities(JSContext *, JSValue *, int, JSValue *);
JSValue js_ble_open(JSContext *, JSValue *, int, JSValue *);

JSValue js_ble_adapter_constructor(JSContext *, JSValue *, int, JSValue *);
void js_ble_adapter_finalizer(JSContext *, void *);
JSValue js_ble_adapter_status(JSContext *, JSValue *, int, JSValue *);
JSValue js_ble_adapter_scan(JSContext *, JSValue *, int, JSValue *);
JSValue js_ble_adapter_connect(JSContext *, JSValue *, int, JSValue *);
JSValue js_ble_adapter_advertise(JSContext *, JSValue *, int, JSValue *);
JSValue js_ble_adapter_server(JSContext *, JSValue *, int, JSValue *);
JSValue js_ble_adapter_bonds(JSContext *, JSValue *, int, JSValue *);
JSValue js_ble_adapter_remove_bond(JSContext *, JSValue *, int, JSValue *);
JSValue js_ble_adapter_clear_bonds(JSContext *, JSValue *, int, JSValue *);
JSValue js_ble_adapter_close(JSContext *, JSValue *, int, JSValue *);

JSValue js_ble_scanner_constructor(JSContext *, JSValue *, int, JSValue *);
void js_ble_scanner_finalizer(JSContext *, void *);
JSValue js_ble_scanner_receive(JSContext *, JSValue *, int, JSValue *);
JSValue js_ble_scanner_stats(JSContext *, JSValue *, int, JSValue *);
JSValue js_ble_scanner_status(JSContext *, JSValue *, int, JSValue *);
JSValue js_ble_scanner_close(JSContext *, JSValue *, int, JSValue *);

JSValue js_ble_advertiser_constructor(JSContext *, JSValue *, int, JSValue *);
void js_ble_advertiser_finalizer(JSContext *, void *);
JSValue js_ble_advertiser_receive(JSContext *, JSValue *, int, JSValue *);
JSValue js_ble_advertiser_stats(JSContext *, JSValue *, int, JSValue *);
JSValue js_ble_advertiser_status(JSContext *, JSValue *, int, JSValue *);
JSValue js_ble_advertiser_close(JSContext *, JSValue *, int, JSValue *);

JSValue js_ble_connection_constructor(JSContext *, JSValue *, int, JSValue *);
void js_ble_connection_finalizer(JSContext *, void *);
JSValue js_ble_connection_receive(JSContext *, JSValue *, int, JSValue *);
JSValue js_ble_connection_stats(JSContext *, JSValue *, int, JSValue *);
JSValue js_ble_connection_status(JSContext *, JSValue *, int, JSValue *);
JSValue js_ble_connection_pair(JSContext *, JSValue *, int, JSValue *);
JSValue js_ble_connection_respond_pairing(JSContext *, JSValue *, int,
                                          JSValue *);
JSValue js_ble_connection_exchange_mtu(JSContext *, JSValue *, int,
                                       JSValue *);
JSValue js_ble_connection_read_rssi(JSContext *, JSValue *, int, JSValue *);
JSValue js_ble_connection_discover(JSContext *, JSValue *, int, JSValue *);
JSValue js_ble_connection_close(JSContext *, JSValue *, int, JSValue *);

JSValue js_ble_service_constructor(JSContext *, JSValue *, int, JSValue *);
void js_ble_service_finalizer(JSContext *, void *);
JSValue js_ble_service_characteristics(JSContext *, JSValue *, int, JSValue *);

JSValue js_ble_characteristic_constructor(JSContext *, JSValue *, int,
                                          JSValue *);
void js_ble_characteristic_finalizer(JSContext *, void *);
JSValue js_ble_characteristic_descriptors(JSContext *, JSValue *, int,
                                          JSValue *);
JSValue js_ble_characteristic_read(JSContext *, JSValue *, int, JSValue *);
JSValue js_ble_characteristic_write(JSContext *, JSValue *, int, JSValue *);
JSValue js_ble_characteristic_subscribe(JSContext *, JSValue *, int,
                                        JSValue *);

JSValue js_ble_descriptor_constructor(JSContext *, JSValue *, int, JSValue *);
void js_ble_descriptor_finalizer(JSContext *, void *);
JSValue js_ble_descriptor_read(JSContext *, JSValue *, int, JSValue *);
JSValue js_ble_descriptor_write(JSContext *, JSValue *, int, JSValue *);

JSValue js_ble_notification_constructor(JSContext *, JSValue *, int,
                                        JSValue *);
void js_ble_notification_finalizer(JSContext *, void *);
JSValue js_ble_notification_receive(JSContext *, JSValue *, int, JSValue *);
JSValue js_ble_notification_stats(JSContext *, JSValue *, int, JSValue *);
JSValue js_ble_notification_status(JSContext *, JSValue *, int, JSValue *);
JSValue js_ble_notification_close(JSContext *, JSValue *, int, JSValue *);

JSValue js_ble_gatt_server_constructor(JSContext *, JSValue *, int, JSValue *);
void js_ble_gatt_server_finalizer(JSContext *, void *);
JSValue js_ble_gatt_server_status(JSContext *, JSValue *, int, JSValue *);
JSValue js_ble_gatt_server_watch(JSContext *, JSValue *, int, JSValue *);
JSValue js_ble_gatt_server_characteristic(JSContext *, JSValue *, int,
                                          JSValue *);

JSValue js_ble_local_characteristic_constructor(JSContext *, JSValue *, int,
                                                JSValue *);
void js_ble_local_characteristic_finalizer(JSContext *, void *);
JSValue js_ble_local_characteristic_value(JSContext *, JSValue *, int,
                                          JSValue *);
JSValue js_ble_local_characteristic_set_value(JSContext *, JSValue *, int,
                                              JSValue *);
JSValue js_ble_local_characteristic_notify(JSContext *, JSValue *, int,
                                           JSValue *);

#endif
