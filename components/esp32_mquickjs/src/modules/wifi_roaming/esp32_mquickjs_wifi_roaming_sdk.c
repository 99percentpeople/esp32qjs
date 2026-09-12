#include "esp32_mquickjs_wifi_roaming.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
#include <stdio.h>
#include <string.h>

bool esp32_mquickjs_wifi_btm_encode(const esp32_mquickjs_wifi_btm_query_t *q, char *text, size_t capacity)
{
    if (q == NULL || text == NULL || capacity == 0 || q->count > ESP32_MQUICKJS_WIFI_BTM_MAX_CANDIDATES ||
        q->reason > 9) return false;
    text[0] = 0;
    size_t used = 0;
    for (unsigned i = 0; i < q->count; ++i) {
        const esp32_mquickjs_wifi_btm_candidate_t *c = &q->candidates[i];
        uint8_t nonzero = 0;
        for (unsigned j = 0; j < 6; ++j) nonzero |= c->bssid[j];
        if (!nonzero || (c->bssid[0] & 1U) || c->operating_class == 0 || c->channel == 0 || c->channel > 233) goto fail;
        for (unsigned j = 0; j < i; ++j) if (!memcmp(c->bssid, q->candidates[j].bssid, 6)) goto fail;
        /* Fixed SDK parses with strtol (32-bit long on ESP32). Signed decimal
         * preserves all BSSID information bits, including 0x80000000..ffffffff. */
        int written = snprintf(text + used, capacity - used,
            " neighbor=%02x:%02x:%02x:%02x:%02x:%02x,%ld,%u,%u,%u",
            c->bssid[0], c->bssid[1], c->bssid[2], c->bssid[3], c->bssid[4], c->bssid[5],
            (long)(int32_t)c->information, c->operating_class, c->channel, c->phy_type);
        if (written < 0 || (size_t)written >= capacity - used) goto fail;
        used += (size_t)written;
        if (c->preference_set) {
            written = snprintf(text + used, capacity - used, ",0301%02x", c->preference);
            if (written < 0 || (size_t)written >= capacity - used) goto fail;
            used += (size_t)written;
        }
    }
    return true;
fail:
    text[0] = 0;
    return false;
}

#if CONFIG_ESP_WIFI_RRM_SUPPORT || CONFIG_ESP_WIFI_WNM_SUPPORT
#include "esp_rrm.h"
#include "esp_wnm.h"
#include "esp32_mquickjs_wireless_core.h"
struct os_reltime;
#include "utils/eloop.h"

typedef struct {
    const esp32_mquickjs_wifi_btm_query_t *query;
    const char *candidates;
    esp32_mquickjs_wifi_roaming_result_t *result;
} roaming_sdk_call_t;

static int roaming_sdk_dispatch(void *opaque, void *unused)
{
    (void)unused;
    roaming_sdk_call_t *call = opaque;
    esp32_mquickjs_wifi_roaming_result_t *r = call->result;
    r->entered = true;
    if (call->query != NULL) {
        wifi_ap_record_t ap;
        r->stage = "station-link";
        r->error = esp_wifi_sta_get_ap_info(&ap);
        if (r->error != ESP_OK) return 0;
    }
    r->stage = "supplicant-capabilities";
#if CONFIG_ESP_WIFI_RRM_SUPPORT
    r->rrm = esp_rrm_is_rrm_supported_connection();
#endif
#if CONFIG_ESP_WIFI_WNM_SUPPORT
    r->btm = esp_wnm_is_btm_supported_connection();
    if (call->query != NULL) {
        if (!r->btm) { r->error = ESP_ERR_NOT_SUPPORTED; return 0; }
        /* Require the explicit Station option; do not silently enable BTM or
         * mutate credentials/configuration to make this request work. */
        wifi_config_t config;
        r->stage = "station-btm-configuration";
        r->error = esp_wifi_get_config(WIFI_IF_STA, &config);
        bool enabled = r->error == ESP_OK && config.sta.btm_enabled;
        esp32_mquickjs_wireless_secure_zero(&config, sizeof(config));
        if (r->error != ESP_OK) return 0;
        if (!enabled) { r->error = ESP_ERR_INVALID_STATE; return 0; }
        r->stage = "btm-query-submit";
        r->submitted = true;
        /* No automatic scan-cache list: only the bounded, typed candidates
         * encoded by the framework. SDK copies/frees its frame before return. */
        r->sdk_code = esp_wnm_send_bss_transition_mgmt_query(call->query->reason,
            call->query->count ? call->candidates : NULL, 0);
        r->error = r->sdk_code == 0 ? ESP_OK : ESP_FAIL;
    }
#endif
    return 0;
}

esp_err_t esp32_mquickjs_wifi_roaming_sdk(const esp32_mquickjs_wifi_btm_query_t *query,
    esp32_mquickjs_wifi_roaming_result_t *r)
{
    if (r == NULL) return ESP_ERR_INVALID_ARG;
#if !CONFIG_ESP_WIFI_WNM_SUPPORT
    if (query != NULL) { r->error = ESP_ERR_NOT_SUPPORTED; return r->error; }
#endif
    char text[ESP32_MQUICKJS_WIFI_BTM_TEXT_BYTES];
    if (query != NULL && !esp32_mquickjs_wifi_btm_encode(query, text, sizeof(text))) return ESP_ERR_INVALID_ARG;
    r->stage = "supplicant-dispatch";
    roaming_sdk_call_t call = {.query = query, .candidates = text, .result = r};
    /* fff9895c82 eloop waits through handler completion or destruction before
     * releasing the waiter. No queued callback retains this stack after return.
     * The handler never takes the Radio mutex or calls JS/default-loop waits. */
    int ret = eloop_register_timeout_blocking(roaming_sdk_dispatch, &call, NULL);
    if (ret != 0 || !r->entered) { r->error = ESP_FAIL; return r->error; }
    if (r->error == ESP_OK) r->stage = "complete";
    return r->error;
}
#endif
#endif
