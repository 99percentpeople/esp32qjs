#pragma once

#include "esp32_mquickjs_types.h"

bool esp32_mquickjs_init_secure_random(JSContext *ctx);
JSValue js_sys_config(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_sys_version_get(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv, int magic);
JSValue js_sys_feature_get(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv, int magic);
JSValue js_sys_hardware_get(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv, int magic);
JSValue js_sys_hardware_chip(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_sys_hardware_cpu(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_sys_hardware_flash(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_sys_hardware_psram(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_sys_runtime_info_get(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv, int magic);
JSValue js_sys_runtime_info_heap(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_sys_runtime_info_task(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_sys_runtime_info_startup(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_sys_runtime_info_filesystem(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_sys_runtime_info_control(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_sys_boot_get(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv, int magic);
JSValue js_sys_boot_reset(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_sys_boot_wakeup(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_sys_cpu_frequency(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_sys_memory_get(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv, int magic);
JSValue js_sys_rtos_get(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv, int magic);
JSValue js_sys_rtos_runtime_task(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_sys_runtime_status_get(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv, int magic);
JSValue js_sys_runtime_status_filesystem(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_sys_runtime_status_resources(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_sys_runtime_status_watchdog(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_sys_runtime_status_startup(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_sys_safe_mode_get(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_sys_safe_mode_set(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_sys_tasks(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_sys_restart_runtime(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_sys_reboot(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_sys_millis(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_sys_micros(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_sys_freeHeap(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_sys_randomHex(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_sys_withTimeout(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
