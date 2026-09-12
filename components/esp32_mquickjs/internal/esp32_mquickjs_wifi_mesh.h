#pragma once
#include "esp32_mquickjs_types.h"
#include "esp32_mquickjs_wifi_mesh_feature.h"
#if ESP32_MQUICKJS_WIFI_MESH_AVAILABLE
bool esp32_mquickjs_init_wifi_mesh_runtime(JSContext *, esp32_mquickjs_runtime_t *);
bool esp32_mquickjs_wifi_mesh_poll_observations(bool close);
JSValue js_wifi_mesh_capabilities(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_mesh_open(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_mesh_constructor(JSContext *, JSValue *, int, JSValue *);
void js_wifi_mesh_finalizer(JSContext *, void *);
JSValue js_wifi_mesh_status(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_mesh_ready(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_mesh_close(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_mesh_cancel(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_mesh_recover(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_mesh_send(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_mesh_receive(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_mesh_watch(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_mesh_routing_table(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_mesh_groups(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_mesh_add_groups(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_mesh_remove_groups(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_mesh_set_tods(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_mesh_connect(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_mesh_disconnect(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_mesh_flush_upstream(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_mesh_configuration(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_mesh_set_router(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_mesh_set_mesh_id(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_mesh_set_type(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_mesh_set_self_organized(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_mesh_set_fixed_root(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_mesh_set_root_conflicts(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_mesh_set_association_expiry(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_mesh_set_root_healing_delay(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_mesh_set_ie_encryption(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_mesh_waive_root(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_mesh_switch_channel(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_mesh_set_device_duty(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_mesh_set_network_duty(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_mesh_signal_duty(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_mesh_subnet(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_mesh_has_group(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_mesh_upstream_capacity(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_mesh_power_status(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_mesh_tsf_time(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_mesh_set_parent(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_mesh_scan(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_mesh_receive_scan(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_mesh_flush_scan(JSContext *, JSValue *, int, JSValue *);
#endif
