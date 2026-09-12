#include "esp32_mquickjs_wifi_promiscuous_driver.h"

#if CONFIG_ESP32_MQUICKJS_WIFI_RADIO
#include <string.h>
#include "esp_timer.h"
#include "esp32_mquickjs_wifi_promiscuous_broker.h"

enum { PROMISCUOUS_CONTROL, PROMISCUOUS_PACKET, PROMISCUOUS_CALLBACK, PROMISCUOUS_ENABLE };
static struct {
    esp32_mquickjs_wifi_promiscuous_driver_status_t status;
    esp32_mquickjs_wifi_promiscuous_demand_t baseline, current;
    uint8_t rollback[4], rollback_count;
} s_promiscuous_driver;

esp_err_t esp32_mquickjs_wifi_promiscuous_driver_demand(
    const esp32_mquickjs_wifi_promiscuous_snapshot_t *requirements,
    bool enabled, bool preserve_baseline, esp32_mquickjs_wifi_promiscuous_demand_t *output)
{
    if (requirements == NULL || output == NULL) return ESP_ERR_INVALID_ARG;
    bool receive = requirements->active != 0 || requirements->reserved != 0;
    if ((requirements->required_types & ~ESP32_MQUICKJS_WIFI_RX_FILTER_TYPE_MASK) != 0 ||
        (receive && !enabled)) return ESP_ERR_INVALID_STATE;
    esp32_mquickjs_wifi_promiscuous_demand_t demand = {
        .enabled = enabled, .receive = receive, .preserve_baseline = preserve_baseline,
    };
    if (requirements->required_types & (1U << ESP32_MQUICKJS_WIFI_PACKET_MANAGEMENT))
        demand.packet_mask |= WIFI_PROMIS_FILTER_MASK_MGMT;
    if (requirements->required_types & (1U << ESP32_MQUICKJS_WIFI_PACKET_CONTROL))
        demand.packet_mask |= WIFI_PROMIS_FILTER_MASK_CTRL;
    if (requirements->required_types & (1U << ESP32_MQUICKJS_WIFI_PACKET_DATA))
        demand.packet_mask |= WIFI_PROMIS_FILTER_MASK_DATA;
    if (requirements->required_types & (1U << ESP32_MQUICKJS_WIFI_PACKET_MISC))
        demand.packet_mask |= WIFI_PROMIS_FILTER_MASK_MISC;
    if (requirements->require_error_frames) demand.packet_mask |= WIFI_PROMIS_FILTER_MASK_FCSFAIL;
    if (demand.packet_mask & WIFI_PROMIS_FILTER_MASK_CTRL) {
        static const uint32_t masks[] = {
            WIFI_PROMIS_CTRL_FILTER_MASK_WRAPPER, WIFI_PROMIS_CTRL_FILTER_MASK_BAR,
            WIFI_PROMIS_CTRL_FILTER_MASK_BA, WIFI_PROMIS_CTRL_FILTER_MASK_PSPOLL,
            WIFI_PROMIS_CTRL_FILTER_MASK_RTS, WIFI_PROMIS_CTRL_FILTER_MASK_CTS,
            WIFI_PROMIS_CTRL_FILTER_MASK_ACK, WIFI_PROMIS_CTRL_FILTER_MASK_CFEND,
            WIFI_PROMIS_CTRL_FILTER_MASK_CFENDACK,
        };
        /* SDK only names individual control masks for subtypes 7..15. For
         * other requests, ask for all driver-supported control traffic, then
         * apply the exact native subtype predicate. This cannot manufacture
         * a frame the SDK itself does not deliver. */
        if (requirements->required_control_subtypes & 0x007fU)
            demand.control_mask = WIFI_PROMIS_CTRL_FILTER_MASK_ALL;
        else for (unsigned i = 0; i < 9; ++i)
            if (requirements->required_control_subtypes & (1U << (i + 7U))) demand.control_mask |= masks[i];
    }
    *output = demand;
    return ESP_OK;
}

static void wifi_promiscuous_callback(void *buffer, wifi_promiscuous_pkt_type_t type)
{
    /* Boot-owned registry, never a session pointer in the SDK callback. */
    (void)esp32_mquickjs_wifi_promiscuous_dispatch(buffer, type, (uint64_t)esp_timer_get_time());
}

static esp_err_t wifi_promiscuous_write(unsigned field,
    const esp32_mquickjs_wifi_promiscuous_demand_t *target, bool restoring, const char **stage)
{
    static const char *const write_stages[] = {
        "promiscuous-control-filter", "promiscuous-filter", "promiscuous-callback", "promiscuous-enable",
    };
    static const char *const read_stages[] = {
        "promiscuous-control-readback", "promiscuous-filter-readback", "promiscuous-callback", "promiscuous-enable-readback",
    };
    static const char *const restore_stages[] = {
        "promiscuous-restore-control", "promiscuous-restore-filter", "promiscuous-restore-callback", "promiscuous-restore-enable",
    };
    *stage = restoring ? restore_stages[field] : write_stages[field];
    if (field == PROMISCUOUS_CALLBACK)
        return esp_wifi_set_promiscuous_rx_cb(target->receive ? wifi_promiscuous_callback : NULL);
    esp_err_t err;
    if (field == PROMISCUOUS_ENABLE) {
        err = esp_wifi_set_promiscuous(target->enabled);
        if (err != ESP_OK) return err;
        if (!restoring) *stage = read_stages[field];
        bool enabled = false;
        err = esp_wifi_get_promiscuous(&enabled);
        return err != ESP_OK ? err : (enabled == target->enabled ? ESP_OK : ESP_FAIL);
    }
    wifi_promiscuous_filter_t requested = {
        .filter_mask = field == PROMISCUOUS_CONTROL ? target->control_mask : target->packet_mask,
    }, actual = {0};
    err = field == PROMISCUOUS_CONTROL ? esp_wifi_set_promiscuous_ctrl_filter(&requested)
        : esp_wifi_set_promiscuous_filter(&requested);
    if (err != ESP_OK) return err;
    if (!restoring) *stage = read_stages[field];
    err = field == PROMISCUOUS_CONTROL ? esp_wifi_get_promiscuous_ctrl_filter(&actual)
        : esp_wifi_get_promiscuous_filter(&actual);
    return err != ESP_OK ? err : (actual.filter_mask == requested.filter_mask ? ESP_OK : ESP_FAIL);
}

esp_err_t esp32_mquickjs_wifi_promiscuous_driver_recover(void)
{
    while (s_promiscuous_driver.rollback_count != 0) {
        unsigned field = s_promiscuous_driver.rollback[s_promiscuous_driver.rollback_count - 1U];
        const char *stage = NULL;
        esp_err_t err = wifi_promiscuous_write(field, &s_promiscuous_driver.current, true, &stage);
        if (err != ESP_OK) {
            s_promiscuous_driver.status.cleanup_pending = true;
            s_promiscuous_driver.status.cleanup_stage = stage;
            s_promiscuous_driver.status.cleanup_error = err;
            return err;
        }
        --s_promiscuous_driver.rollback_count;
    }
    s_promiscuous_driver.status.cleanup_pending = false;
    s_promiscuous_driver.status.cleanup_stage = NULL;
    s_promiscuous_driver.status.cleanup_error = ESP_OK;
    if (!s_promiscuous_driver.current.enabled) s_promiscuous_driver.status.claimed = false;
    return ESP_OK;
}

void esp32_mquickjs_wifi_promiscuous_driver_status(
    esp32_mquickjs_wifi_promiscuous_driver_status_t *output)
{
    if (output != NULL) *output = s_promiscuous_driver.status;
}

esp_err_t esp32_mquickjs_wifi_promiscuous_driver_apply(
    const esp32_mquickjs_wifi_promiscuous_demand_t *demand)
{
    if (demand == NULL || (demand->receive && !demand->enabled)) return ESP_ERR_INVALID_ARG;
    if (s_promiscuous_driver.status.cleanup_pending) return ESP_ERR_INVALID_STATE;
    esp_err_t err = ESP_OK;
    const char *stage = NULL;
    s_promiscuous_driver.status.error = ESP_OK;
    s_promiscuous_driver.status.error_stage = NULL;
    if (!s_promiscuous_driver.status.claimed) {
        if (!demand->enabled) return ESP_OK;
        bool enabled = false;
        stage = "promiscuous-snapshot-enable";
        err = esp_wifi_get_promiscuous(&enabled);
        if (err != ESP_OK) goto fail;
        if (enabled) { err = ESP_ERR_INVALID_STATE; goto fail; }
        wifi_promiscuous_filter_t packet = {0}, control = {0};
        stage = "promiscuous-snapshot-filter";
        err = esp_wifi_get_promiscuous_filter(&packet);
        if (err != ESP_OK) goto fail;
        stage = "promiscuous-snapshot-control";
        err = esp_wifi_get_promiscuous_ctrl_filter(&control);
        if (err != ESP_OK) goto fail;
        s_promiscuous_driver.baseline = (esp32_mquickjs_wifi_promiscuous_demand_t){
            .packet_mask = packet.filter_mask, .control_mask = control.filter_mask,
        };
        s_promiscuous_driver.current = s_promiscuous_driver.baseline;
        s_promiscuous_driver.status.claimed = true;
    }
    esp32_mquickjs_wifi_promiscuous_demand_t target = s_promiscuous_driver.baseline;
    target.enabled = demand->enabled;
    if (demand->enabled && demand->receive) {
        target = *demand;
        if (demand->preserve_baseline) {
            target.packet_mask |= s_promiscuous_driver.baseline.packet_mask;
            target.control_mask |= s_promiscuous_driver.baseline.control_mask;
        }
    }
    const esp32_mquickjs_wifi_promiscuous_demand_t *previous = &s_promiscuous_driver.current;
    bool changed[] = {
        target.control_mask != previous->control_mask,
        target.packet_mask != previous->packet_mask,
        target.receive != previous->receive,
        target.enabled != previous->enabled,
    };
    /* Enable only after callback/filter setup. Stop admission at the driver
     * before final callback unregister and filter restoration. */
    for (unsigned i = 0; i < 4; ++i) {
        unsigned field = target.enabled ? i : 3U - i;
        if (!changed[field]) continue;
        /* Even an error may have mutated native state. Record before the call. */
        s_promiscuous_driver.rollback[s_promiscuous_driver.rollback_count++] = (uint8_t)field;
        err = wifi_promiscuous_write(field, &target, false, &stage);
        if (err != ESP_OK) goto fail;
    }
    s_promiscuous_driver.rollback_count = 0;
    s_promiscuous_driver.current = target;
    if (!target.enabled) s_promiscuous_driver.status.claimed = false;
    return ESP_OK;
fail:
    s_promiscuous_driver.status.error = err;
    s_promiscuous_driver.status.error_stage = stage;
    /* Keep the original failure separate from a rollback failure. */
    (void)esp32_mquickjs_wifi_promiscuous_driver_recover();
    return err;
}
#endif
