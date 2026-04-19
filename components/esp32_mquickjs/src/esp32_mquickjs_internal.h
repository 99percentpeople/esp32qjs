#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp32_mquickjs.h"

#define ESP32_MQUICKJS_BRIDGE_NAMESPACE "__esp32__"
#define ESP32_MQUICKJS_MAX_TIMERS 16
#define ESP32_MQUICKJS_TIMER_QUEUE_LEN 16
#define ESP32_MQUICKJS_MAX_SCRIPT_PATH 256
#define ESP32_MQUICKJS_USER_LED_PIN 21
#define ESP32_MQUICKJS_USER_LED_ACTIVE_LOW 1

bool esp32_mquickjs_set_property(JSContext *ctx,
                                 JSValue target_obj,
                                 const char *name,
                                 JSValue value);

bool esp32_mquickjs_set_alias(JSContext *ctx,
                              JSValue target_obj,
                              JSValue source_obj,
                              const char *target_name,
                              const char *source_name);

bool esp32_mquickjs_set_bound_bridge_function(JSContext *ctx,
                                              JSValue target_obj,
                                              JSValue global_obj,
                                              const char *target_name,
                                              const char *operation);

esp32_mquickjs_runtime_t *esp32_mquickjs_get_active_runtime(void);

JSValue esp32_mquickjs_load_from_littlefs(JSContext *ctx,
                                          esp32_mquickjs_runtime_t *runtime,
                                          const char *script_path);

bool esp32_mquickjs_mount_littlefs(bool format_if_mount_failed);

bool esp32_mquickjs_install_fs_module(JSContext *ctx, JSValue global_obj);
bool esp32_mquickjs_install_gpio_module(JSContext *ctx, JSValue global_obj);
bool esp32_mquickjs_install_esp32_module(JSContext *ctx, JSValue global_obj);

bool esp32_mquickjs_dispatch_fs(JSContext *ctx,
                                const char *operation,
                                int argc,
                                JSValue *argv,
                                JSValue *result);

bool esp32_mquickjs_dispatch_gpio(JSContext *ctx,
                                  const char *operation,
                                  int argc,
                                  JSValue *argv,
                                  JSValue *result);

bool esp32_mquickjs_dispatch_esp32(JSContext *ctx,
                                   const char *operation,
                                   int argc,
                                   JSValue *argv,
                                   JSValue *result);
