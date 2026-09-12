#pragma once
#include "esp_wifi.h"
#include <stdbool.h>
#include <string.h>

static inline wifi_scan_default_params_t wifi_scan_parameters_effective(
    const wifi_scan_default_params_t *input)
{
    wifi_scan_default_params_t value = WIFI_SCAN_PARAMS_DEFAULT_CONFIG();
    if (input != NULL) {
        value.scan_time.active.min = input->scan_time.active.min;
        if (input->scan_time.active.max) value.scan_time.active.max = input->scan_time.active.max;
        if (input->scan_time.passive) value.scan_time.passive = input->scan_time.passive;
        if (input->home_chan_dwell_time) value.home_chan_dwell_time = input->home_chan_dwell_time;
    }
    return value;
}

static inline bool wifi_scan_parameters_valid(const wifi_scan_default_params_t *input)
{
    if (input == NULL) return true; /* Public SDK reset request. */
    wifi_scan_default_params_t value = wifi_scan_parameters_effective(input);
    return value.scan_time.active.min <= value.scan_time.active.max &&
        value.scan_time.active.max <= 1500 && value.scan_time.passive <= 1500 &&
        value.home_chan_dwell_time >= 30 && value.home_chan_dwell_time <= 150;
}

static inline bool wifi_scan_parameters_equal(const wifi_scan_default_params_t *a,
    const wifi_scan_default_params_t *b)
{
    return a->scan_time.active.min == b->scan_time.active.min &&
        a->scan_time.active.max == b->scan_time.active.max &&
        a->scan_time.passive == b->scan_time.passive &&
        a->home_chan_dwell_time == b->home_chan_dwell_time;
}

static inline bool wifi_scan_parameters_observed_valid(const wifi_scan_default_params_t *value)
{
    wifi_scan_default_params_t effective = wifi_scan_parameters_effective(value);
    return wifi_scan_parameters_valid(value) && wifi_scan_parameters_equal(value, &effective);
}

/* Fixed SDK ieee80211_api.o copies 44 bytes before its native handler consumes
 * the 16-byte public structure. The zeroed union bounds that excess read and
 * prevents unrelated stack bytes entering the native command. The public SDK
 * still owns validation, submission, and its normal null/reset path. */
static inline esp_err_t wifi_scan_parameters_set_native(const wifi_scan_default_params_t *input)
{
    _Static_assert(sizeof(wifi_scan_default_params_t) == 16, "Review SDK scan-default structure");
    if (input == NULL) return esp_wifi_set_scan_parameters(NULL);
    union {
        wifi_scan_default_params_t parameters;
        unsigned char bytes[44];
    } storage = {0};
    memcpy(storage.bytes, input, sizeof(*input));
    return esp_wifi_set_scan_parameters(&storage.parameters);
}
