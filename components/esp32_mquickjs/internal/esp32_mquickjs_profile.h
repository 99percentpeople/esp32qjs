#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    ESP32_MQUICKJS_PROFILE_VALUE_INTEGER = 1,
    ESP32_MQUICKJS_PROFILE_VALUE_BOOLEAN = 2,
    ESP32_MQUICKJS_PROFILE_VALUE_STRING = 3,
} esp32_mquickjs_profile_value_type_t;

typedef struct {
    esp32_mquickjs_profile_value_type_t type;
    union {
        int32_t integer;
        bool boolean;
        const char *string;
    } value;
} esp32_mquickjs_profile_value_t;

bool esp32_mquickjs_profile_get(const char *key,
                                esp32_mquickjs_profile_value_t *out_value);
bool esp32_mquickjs_profile_get_int(const char *key, int32_t *out_value);
bool esp32_mquickjs_profile_get_bool(const char *key, bool *out_value);
int32_t esp32_mquickjs_profile_int_or(const char *key, int32_t fallback);
uint32_t esp32_mquickjs_profile_uint_or(const char *key, uint32_t fallback);
bool esp32_mquickjs_profile_bool_or(const char *key, bool fallback);
