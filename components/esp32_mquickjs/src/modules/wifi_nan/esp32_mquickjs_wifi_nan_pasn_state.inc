/* Included only in the hash-pinned supplicant copy. All writers and snapshots
 * run on the Wi-Fi task. Numeric timer identity never grants access to a freed
 * context, including allocator reuse at the same address. */
#include "esp32_mquickjs_wifi_nan_pasn_sdk.h"
#include "esp32_mquickjs_memory.h"
#include "esp_timer.h"
#include "esp32_mquickjs_wifi_nan_tx.h"

extern bool current_task_is_wifi_task(void);
static uint32_t s_esp32qjs_pasn_last_identity;
static esp_err_t s_esp32qjs_pasn_create_error;
static esp32_mquickjs_wifi_nan_pasn_status_t s_esp32qjs_pasn_status;

uint32_t esp32_mquickjs_wifi_nan_pasn_identity(const uint8_t *peer)
{
    if (!current_task_is_wifi_task() || s_esp32qjs_pasn_status.closing ||
        (peer && memcmp(peer, s_esp32qjs_pasn_status.peer, 6))) return 0;
    return s_esp32qjs_pasn_status.identity;
}

void esp32_mquickjs_wifi_nan_pasn_result(const uint8_t *peer, esp_err_t error, bool paired)
{
    if (!peer || !esp32_mquickjs_wifi_nan_pasn_identity(peer)) return;
    if (error && !s_esp32qjs_pasn_status.error) s_esp32qjs_pasn_status.error = error;
    if (!error) s_esp32qjs_pasn_status.authenticated = true;
    if (paired && !s_esp32qjs_pasn_status.error && !s_esp32qjs_pasn_status.paired) {
        s_esp32qjs_pasn_status.paired = true;
        s_esp32qjs_pasn_status.completed_us = esp_timer_get_time();
    }
}

static void esp32qjs_pasn_failure(struct nan_pasn_data *pd, esp_err_t error)
{
    if (pd && pd == esp_nan_app_get_pasn_data() &&
        pd->framework_identity == s_esp32qjs_pasn_status.identity &&
        !s_esp32qjs_pasn_status.error) s_esp32qjs_pasn_status.error = error;
}

static bool esp32qjs_pasn_peer_valid(const uint8_t *peer)
{
    return peer && !(peer[0] & 1) && !is_zero_ether_addr(peer);
}

static void esp32qjs_pasn_pin_clear(char (*pin)[16])
{
    forced_memzero(*pin, sizeof(*pin));
}
