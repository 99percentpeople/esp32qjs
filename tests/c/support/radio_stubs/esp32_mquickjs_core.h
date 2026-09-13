#pragma once
#include <stdbool.h>
#include <assert.h>
typedef int esp32_mquickjs_runtime_t;
typedef struct { bool active; } esp32_mquickjs_native_wait_t;
static bool radio_test_interrupt;
static unsigned radio_test_wait_depth;
static esp32_mquickjs_runtime_t *esp32_mquickjs_get_active_runtime(void) { static int runtime;return &runtime; }
static bool esp32_mquickjs_cooperate(esp32_mquickjs_runtime_t *runtime) { (void)runtime;return !radio_test_interrupt; }
static void esp32_mquickjs_native_wait_begin(esp32_mquickjs_runtime_t *runtime,esp32_mquickjs_native_wait_t *wait) {
    (void)runtime;wait->active=true;radio_test_wait_depth++;
}
static void esp32_mquickjs_native_wait_end(esp32_mquickjs_runtime_t *runtime,esp32_mquickjs_native_wait_t *wait) {
    (void)runtime;assert(wait->active && radio_test_wait_depth);wait->active=false;radio_test_wait_depth--;
}
