#include "esp32_mquickjs_profile.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

typedef struct {
    const char *key;
    esp32_mquickjs_profile_value_t value;
} esp32_mquickjs_profile_entry_t;

#define ESP32_MQUICKJS_PROFILE_INT(entry_key, entry_value) \
    { (entry_key), { ESP32_MQUICKJS_PROFILE_VALUE_INTEGER, { .integer = (entry_value) } } }
#define ESP32_MQUICKJS_PROFILE_BOOL(entry_key, entry_value) \
    { (entry_key), { ESP32_MQUICKJS_PROFILE_VALUE_BOOLEAN, { .boolean = (entry_value) } } }
#define ESP32_MQUICKJS_PROFILE_STRING(entry_key, entry_value) \
    { (entry_key), { ESP32_MQUICKJS_PROFILE_VALUE_STRING, { .string = (entry_value) } } }

static const esp32_mquickjs_profile_entry_t s_profile_entries[] = {
#include "esp32qjs_profile_constants.inc"
    { NULL, { ESP32_MQUICKJS_PROFILE_VALUE_INTEGER, { .integer = 0 } } },
};

bool esp32_mquickjs_profile_get(const char *key,
                                esp32_mquickjs_profile_value_t *out_value)
{
    size_t index;

    if (key == NULL || key[0] == '\0' || out_value == NULL) {
        return false;
    }
    for (index = 0; s_profile_entries[index].key != NULL; ++index) {
        if (strcmp(s_profile_entries[index].key, key) == 0) {
            *out_value = s_profile_entries[index].value;
            return true;
        }
    }
    return false;
}

bool esp32_mquickjs_profile_get_int(const char *key, int32_t *out_value)
{
    esp32_mquickjs_profile_value_t value;

    if (out_value == NULL || !esp32_mquickjs_profile_get(key, &value) ||
        value.type != ESP32_MQUICKJS_PROFILE_VALUE_INTEGER) {
        return false;
    }
    *out_value = value.value.integer;
    return true;
}

bool esp32_mquickjs_profile_get_bool(const char *key, bool *out_value)
{
    esp32_mquickjs_profile_value_t value;

    if (out_value == NULL || !esp32_mquickjs_profile_get(key, &value) ||
        value.type != ESP32_MQUICKJS_PROFILE_VALUE_BOOLEAN) {
        return false;
    }
    *out_value = value.value.boolean;
    return true;
}

int32_t esp32_mquickjs_profile_int_or(const char *key, int32_t fallback)
{
    int32_t value;

    return esp32_mquickjs_profile_get_int(key, &value) ? value : fallback;
}

uint32_t esp32_mquickjs_profile_uint_or(const char *key, uint32_t fallback)
{
    int32_t value;

    if (!esp32_mquickjs_profile_get_int(key, &value) || value < 0) {
        return fallback;
    }
    return (uint32_t)value;
}

bool esp32_mquickjs_profile_bool_or(const char *key, bool fallback)
{
    bool value;

    return esp32_mquickjs_profile_get_bool(key, &value) ? value : fallback;
}
