#include "esp32_mquickjs_wifi_tx_rate.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
#include <string.h>

enum { RATE_CCK, RATE_OFDM, RATE_MCS, RATE_LR };
static const esp32_mquickjs_wifi_tx_rate_entry_t s_rates[] = {
#define RATE(label, value, family, mcs) {label, value, family, mcs}
    RATE("1m-long", WIFI_PHY_RATE_1M_L, RATE_CCK, 0),
    RATE("2m-long", WIFI_PHY_RATE_2M_L, RATE_CCK, 0),
    RATE("5.5m-long", WIFI_PHY_RATE_5M_L, RATE_CCK, 0),
    RATE("11m-long", WIFI_PHY_RATE_11M_L, RATE_CCK, 0),
    RATE("2m-short", WIFI_PHY_RATE_2M_S, RATE_CCK, 0),
    RATE("5.5m-short", WIFI_PHY_RATE_5M_S, RATE_CCK, 0),
    RATE("11m-short", WIFI_PHY_RATE_11M_S, RATE_CCK, 0),
    RATE("6m", WIFI_PHY_RATE_6M, RATE_OFDM, 0),
    RATE("9m", WIFI_PHY_RATE_9M, RATE_OFDM, 0),
    RATE("12m", WIFI_PHY_RATE_12M, RATE_OFDM, 0),
    RATE("18m", WIFI_PHY_RATE_18M, RATE_OFDM, 0),
    RATE("24m", WIFI_PHY_RATE_24M, RATE_OFDM, 0),
    RATE("36m", WIFI_PHY_RATE_36M, RATE_OFDM, 0),
    RATE("48m", WIFI_PHY_RATE_48M, RATE_OFDM, 0),
    RATE("54m", WIFI_PHY_RATE_54M, RATE_OFDM, 0),
#define MCS(index) \
    RATE("mcs" #index "-long", WIFI_PHY_RATE_MCS##index##_LGI, RATE_MCS, index), \
    RATE("mcs" #index "-short", WIFI_PHY_RATE_MCS##index##_SGI, RATE_MCS, index)
    MCS(0), MCS(1), MCS(2), MCS(3), MCS(4), MCS(5), MCS(6), MCS(7),
#if CONFIG_SOC_WIFI_HE_SUPPORT || !CONFIG_SOC_WIFI_SUPPORTED
    MCS(8), MCS(9),
#endif
#undef MCS
    RATE("lr-250k", WIFI_PHY_RATE_LORA_250K, RATE_LR, 0),
    RATE("lr-500k", WIFI_PHY_RATE_LORA_500K, RATE_LR, 0),
#undef RATE
};

const esp32_mquickjs_wifi_tx_rate_entry_t *esp32_mquickjs_wifi_tx_rate_entries(size_t *count)
{
    if (count != NULL) *count = sizeof(s_rates) / sizeof(*s_rates);
    return s_rates;
}

static const esp32_mquickjs_wifi_tx_rate_entry_t *rate_entry(int32_t rate)
{
    for (unsigned i = 0; i < sizeof(s_rates) / sizeof(*s_rates); ++i)
        if ((int32_t)s_rates[i].rate == rate) return &s_rates[i];
    return NULL;
}
const char *esp32_mquickjs_wifi_tx_rate_name(int32_t rate)
{
    const esp32_mquickjs_wifi_tx_rate_entry_t *entry = rate_entry(rate);
    return entry ? entry->name : NULL;
}
const char *esp32_mquickjs_wifi_tx_phy_name(wifi_phy_mode_t mode)
{
    switch (mode) {
    case WIFI_PHY_MODE_LR: return "lr";
    case WIFI_PHY_MODE_11B: return "11b";
    case WIFI_PHY_MODE_11G: return "11g";
    case WIFI_PHY_MODE_HT20: return "ht20";
    case WIFI_PHY_MODE_HT40: return "ht40";
#if CONFIG_SOC_WIFI_SUPPORT_5G
    case WIFI_PHY_MODE_11A: return "11a";
    case WIFI_PHY_MODE_VHT20: return "vht20";
#endif
#if CONFIG_SOC_WIFI_HE_SUPPORT
    case WIFI_PHY_MODE_HE20: return "he20";
#endif
    default: return NULL;
    }
}

bool esp32_mquickjs_wifi_tx_rate_valid(const wifi_tx_rate_config_t *config)
{
    if (config == NULL || esp32_mquickjs_wifi_tx_phy_name(config->phymode) == NULL) return false;
    const esp32_mquickjs_wifi_tx_rate_entry_t *rate = rate_entry(config->rate);
    if (rate == NULL || ((config->ersu || config->dcm) && config->phymode != WIFI_PHY_MODE_HE20)) return false;
    switch (config->phymode) {
    case WIFI_PHY_MODE_LR: return rate->family == RATE_LR;
    case WIFI_PHY_MODE_11B: return rate->family == RATE_CCK;
    case WIFI_PHY_MODE_11G: case WIFI_PHY_MODE_11A: return rate->family == RATE_OFDM;
    case WIFI_PHY_MODE_HT20: case WIFI_PHY_MODE_HT40: return rate->family == RATE_MCS && rate->mcs <= 7;
    case WIFI_PHY_MODE_HE20: case WIFI_PHY_MODE_VHT20: return rate->family == RATE_MCS && rate->mcs <= 9;
    default: return false;
    }
}

esp_err_t esp32_mquickjs_wifi_tx_rate_apply(esp32_mquickjs_wifi_tx_rate_state_t *state,
    uint32_t generation, wifi_interface_t interface, const wifi_tx_rate_config_t *config,
    esp32_mquickjs_wifi_tx_rate_writer_t writer, void *opaque, esp32_mquickjs_wifi_tx_rate_write_t *output)
{
    if (state == NULL || output == NULL || writer == NULL || generation == 0U ||
        (interface != WIFI_IF_STA && interface != WIFI_IF_AP) || !esp32_mquickjs_wifi_tx_rate_valid(config)) return ESP_ERR_INVALID_ARG;
#if !CONFIG_ESP_WIFI_SOFTAP_SUPPORT
    if (interface == WIFI_IF_AP) return ESP_ERR_NOT_SUPPORTED;
#endif
    *output = (esp32_mquickjs_wifi_tx_rate_write_t){0};
    if (state->next_identity == 0U) return ESP_ERR_INVALID_STATE;
    unsigned index = interface == WIFI_IF_STA ? 0 : 1;
    esp32_mquickjs_wifi_tx_rate_record_t previous = state->records[index];
    wifi_tx_rate_config_t requested = *config;
    uint32_t identity = state->next_identity;
    state->next_identity = identity == UINT32_MAX ? 0U : identity + 1U;
    esp32_mquickjs_wifi_tx_rate_record_t current = {.generation = generation, .write_identity = identity};
    output->identity = identity; output->attempted = true;
    esp_err_t err = writer(opaque, interface, &requested);
    output->error = current.error = err;
    if (err == ESP_OK) {
        output->accepted = current.known = true;
        current.config = requested;
    } else {
        if (previous.known && previous.generation == generation) {
            output->rollback_attempted = true;
            esp_err_t rollback = writer(opaque, interface, &previous.config);
            output->rollback_error = current.rollback_error = rollback;
            if (rollback == ESP_OK) {
                output->restored = current.known = true;
                current.config = previous.config;
            }
        }
        output->uncertain = current.uncertain = !current.known;
    }
    state->records[index] = current;
    return err;
}

esp_err_t esp32_mquickjs_wifi_tx_rate_borrow_admission(const esp32_mquickjs_wifi_tx_rate_state_t *state,
    uint32_t generation, wifi_interface_t interface, const wifi_tx_rate_config_t *config,
    const esp32_mquickjs_wifi_tx_rate_lease_t *lease)
{
    if (state == NULL || lease == NULL || generation == 0U ||
        (interface != WIFI_IF_STA && interface != WIFI_IF_AP) ||
        !esp32_mquickjs_wifi_tx_rate_valid(config)) return ESP_ERR_INVALID_ARG;
#if !CONFIG_ESP_WIFI_SOFTAP_SUPPORT
    if (interface == WIFI_IF_AP) return ESP_ERR_NOT_SUPPORTED;
#endif
    const esp32_mquickjs_wifi_tx_rate_record_t *previous = &state->records[interface == WIFI_IF_STA ? 0 : 1];
    if (lease->identity != 0U || lease->restore_pending || !previous->known || previous->uncertain ||
        previous->generation != generation || previous->write_identity == 0U ||
        !esp32_mquickjs_wifi_tx_rate_valid(&previous->config) ||
        state->next_identity == 0U || state->next_identity == UINT32_MAX) return ESP_ERR_INVALID_STATE;
    return ESP_OK;
}

esp_err_t esp32_mquickjs_wifi_tx_rate_borrow(esp32_mquickjs_wifi_tx_rate_state_t *state,
    uint32_t generation, uint32_t owner_identity, wifi_interface_t interface,
    const wifi_tx_rate_config_t *config, esp32_mquickjs_wifi_tx_rate_lease_t *lease,
    esp32_mquickjs_wifi_tx_rate_writer_t writer, void *opaque)
{
    if (owner_identity == 0U || writer == NULL) return ESP_ERR_INVALID_ARG;
    esp_err_t err = esp32_mquickjs_wifi_tx_rate_borrow_admission(state, generation, interface, config, lease);
    if (err != ESP_OK) return err;
    *lease = (esp32_mquickjs_wifi_tx_rate_lease_t){.generation = generation, .identity = owner_identity,
        .interface = interface, .previous = state->records[interface == WIFI_IF_STA ? 0 : 1].config};
    esp32_mquickjs_wifi_tx_rate_write_t write = {0};
    err = esp32_mquickjs_wifi_tx_rate_apply(state, generation, interface, config, writer, opaque, &write);
    lease->write_identity = write.identity;
    if (err != ESP_OK) {
        if (!write.attempted || write.restored) memset(lease, 0, sizeof(*lease));
        else {
            lease->restore_pending = true;
            lease->restore_error = write.rollback_error != ESP_OK ? write.rollback_error : err;
        }
    }
    return err;
}

esp_err_t esp32_mquickjs_wifi_tx_rate_restore(esp32_mquickjs_wifi_tx_rate_state_t *state,
    uint32_t generation, uint32_t owner_identity, esp32_mquickjs_wifi_tx_rate_lease_t *lease,
    esp32_mquickjs_wifi_tx_rate_writer_t writer, void *opaque)
{
    if (state == NULL || lease == NULL || writer == NULL || generation == 0U || owner_identity == 0U)
        return ESP_ERR_INVALID_ARG;
    if (lease->identity != owner_identity || lease->generation != generation) return ESP_ERR_INVALID_STATE;
    lease->restore_pending = true;
    if ((lease->interface != WIFI_IF_STA && lease->interface != WIFI_IF_AP) || lease->write_identity == 0U ||
        !esp32_mquickjs_wifi_tx_rate_valid(&lease->previous))
        return lease->restore_error = ESP_ERR_INVALID_STATE;
    unsigned index = lease->interface == WIFI_IF_STA ? 0U : 1U;
    if (state->records[index].generation != generation || state->records[index].write_identity != lease->write_identity)
        return lease->restore_error = ESP_ERR_INVALID_STATE;
    /* An uncertain current record is admissible here: its exact write is ours
     * and the independently frozen predecessor remains trustworthy. */
    esp32_mquickjs_wifi_tx_rate_write_t write = {0};
    esp_err_t err = esp32_mquickjs_wifi_tx_rate_apply(state, generation, lease->interface,
        &lease->previous, writer, opaque, &write);
    if (write.attempted) lease->write_identity = write.identity;
    lease->restore_error = err;
    if (err == ESP_OK) memset(lease, 0, sizeof(*lease));
    return err;
}

static bool rate_replay_capacity(const esp32_mquickjs_wifi_tx_rate_state_t *state, uint8_t mask)
{
    unsigned count = (mask & 1U) + ((mask >> 1) & 1U);
    return count == 0U || (state->next_identity != 0U && count - 1U <= UINT32_MAX - state->next_identity);
}

bool esp32_mquickjs_wifi_tx_rate_replay_capacity(const esp32_mquickjs_wifi_tx_rate_state_t *state,
    const esp32_mquickjs_wifi_tx_rate_snapshot_t *snapshot)
{
    return state != NULL && snapshot != NULL && snapshot->generation != 0U &&
        (snapshot->mask & ~3U) == 0U && rate_replay_capacity(state, snapshot->mask);
}

esp_err_t esp32_mquickjs_wifi_tx_rate_restart_capture(const esp32_mquickjs_wifi_tx_rate_state_t *state,
    uint32_t generation, esp32_mquickjs_wifi_tx_rate_snapshot_t *snapshot)
{
    if (snapshot == NULL) return ESP_ERR_INVALID_ARG;
    *snapshot = (esp32_mquickjs_wifi_tx_rate_snapshot_t){0};
    if (state == NULL || generation == 0U) return ESP_ERR_INVALID_ARG;
    esp32_mquickjs_wifi_tx_rate_snapshot_t saved = {.generation = generation};
    for (unsigned i = 0; i < 2; ++i) {
        const esp32_mquickjs_wifi_tx_rate_record_t *record = &state->records[i];
        if (record->write_identity == 0U && !record->known && !record->uncertain) continue;
        if (!record->known || record->uncertain || record->generation != generation ||
            record->write_identity == 0U || !esp32_mquickjs_wifi_tx_rate_valid(&record->config)) return ESP_ERR_INVALID_STATE;
#if !CONFIG_ESP_WIFI_SOFTAP_SUPPORT
        if (i == 1) return ESP_ERR_NOT_SUPPORTED;
#endif
        saved.configs[i] = record->config;
        saved.mask |= 1U << i;
    }
    if (!esp32_mquickjs_wifi_tx_rate_replay_capacity(state, &saved)) return ESP_ERR_INVALID_STATE;
    *snapshot = saved;
    return ESP_OK;
}

esp_err_t esp32_mquickjs_wifi_tx_rate_recovery_capture(const esp32_mquickjs_wifi_tx_rate_state_t *state,
    uint32_t generation, uint32_t owner_identity, const esp32_mquickjs_wifi_tx_rate_lease_t *temporary,
    esp32_mquickjs_wifi_tx_rate_snapshot_t *snapshot)
{
    if (snapshot == NULL) return ESP_ERR_INVALID_ARG;
    *snapshot = (esp32_mquickjs_wifi_tx_rate_snapshot_t){0};
    if (state == NULL || temporary == NULL || generation == 0U || owner_identity == 0U) return ESP_ERR_INVALID_ARG;
    if (temporary->restore_pending) return ESP_ERR_INVALID_STATE;
    bool borrowed = temporary->identity != 0U;
    unsigned index = temporary->interface == WIFI_IF_STA ? 0U : 1U;
    if (borrowed && (temporary->identity != owner_identity || temporary->generation != generation ||
        (temporary->interface != WIFI_IF_STA && temporary->interface != WIFI_IF_AP) ||
        temporary->write_identity == 0U || state->records[index].write_identity != temporary->write_identity ||
        !esp32_mquickjs_wifi_tx_rate_valid(&temporary->previous))) return ESP_ERR_INVALID_STATE;
    esp32_mquickjs_wifi_tx_rate_snapshot_t saved;
    esp_err_t err = esp32_mquickjs_wifi_tx_rate_restart_capture(state, generation, &saved);
    if (err != ESP_OK) return err;
    if (borrowed) {
        /* Keep accepted-record validation/capacity checks, but freeze permanent
         * intent rather than the temporary value currently in the driver. */
        if (!(saved.mask & (1U << index))) return ESP_ERR_INVALID_STATE;
        saved.configs[index] = temporary->previous;
    }
    *snapshot = saved;
    return ESP_OK;
}

esp_err_t esp32_mquickjs_wifi_tx_rate_replay(esp32_mquickjs_wifi_tx_rate_state_t *state,
    uint32_t generation, const esp32_mquickjs_wifi_tx_rate_snapshot_t *snapshot, uint8_t *completed,
    esp32_mquickjs_wifi_tx_rate_writer_t writer, void *opaque, esp32_mquickjs_wifi_tx_rate_write_t *output)
{
    if (output == NULL) return ESP_ERR_INVALID_ARG;
    *output = (esp32_mquickjs_wifi_tx_rate_write_t){0};
    if (state == NULL || snapshot == NULL || completed == NULL || writer == NULL || generation == 0U)
        return output->error = ESP_ERR_INVALID_ARG;
    if (snapshot->generation == 0U || snapshot->generation == generation || (snapshot->mask & ~3U) != 0U ||
        (*completed & ~snapshot->mask) != 0U || !rate_replay_capacity(state, snapshot->mask & ~*completed))
        return output->error = ESP_ERR_INVALID_STATE;
    for (unsigned i = 0; i < 2; ++i) {
        if (!(snapshot->mask & (1U << i))) continue;
        if (!esp32_mquickjs_wifi_tx_rate_valid(&snapshot->configs[i])) return output->error = ESP_ERR_INVALID_ARG;
#if !CONFIG_ESP_WIFI_SOFTAP_SUPPORT
        if (i == 1) return output->error = ESP_ERR_NOT_SUPPORTED;
#endif
    }
    for (unsigned i = 0; i < 2; ++i) {
        if (!(snapshot->mask & ~*completed & (1U << i))) continue;
        esp_err_t err = esp32_mquickjs_wifi_tx_rate_apply(state, generation, i == 0 ? WIFI_IF_STA : WIFI_IF_AP,
            &snapshot->configs[i], writer, opaque, output);
        if (err != ESP_OK) return err;
        *completed |= 1U << i;
    }
    return ESP_OK;
}

void esp32_mquickjs_wifi_tx_rate_invalidate(esp32_mquickjs_wifi_tx_rate_state_t *state)
{
    if (state != NULL) memset(state->records, 0, sizeof(state->records));
}
bool esp32_mquickjs_wifi_tx_rate_uncertain(const esp32_mquickjs_wifi_tx_rate_state_t *state)
{
    return state != NULL && (state->records[0].uncertain || state->records[1].uncertain);
}
#endif
