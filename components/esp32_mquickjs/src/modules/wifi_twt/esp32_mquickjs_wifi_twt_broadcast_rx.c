#include "esp32_mquickjs_wifi_twt_broadcast_timer.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_IDF_TARGET_ESP32C5
#include <stddef.h>
#include <string.h>
/* Reviewed action dispatcher preserves four arguments: node, MAC header,
 * action-body pointer, and one-past-end pointer (not a byte count). */
int __real_he_recv_action_twt_setup(void *node, const void *header, const uint8_t *body, const uint8_t *end);
int __wrap_he_recv_action_twt_setup(void *node, const void *header, const uint8_t *body, const uint8_t *end)
{
    uintptr_t first = (uintptr_t)body, last = (uintptr_t)end;
    if (body == NULL || end == NULL || last < first || last - first < 6U || body[3] != 216 ||
        !((body[0] == 22 && body[1] == 6) || (body[0] == 23 && body[1] == 4))) return -1;
    if ((body[5] & 12U) == 12U) {
        if (body[4] != 10 || last - first < 15U) return -1;
        /* The SDK copies seventeen parameter bytes even though the broadcast
         * IE has only twelve. Never borrow allocation tail or following IEs. */
        uint8_t normalized[20] = {0};
        memcpy(normalized, body, 15);
        return esp32_mquickjs_wifi_btwt_response_native((uintptr_t)node, normalized);
    }
    if (body[4] != 15 || last - first < 20U) return -1;
    return __real_he_recv_action_twt_setup(node, header, body, end);
}
#endif
