#include "sdkconfig.h"
#include "esp_wifi.h"
#include <string.h>

#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
/* Private bridge for the hash-pinned SDK rate validator. STOP frees g_ic's
 * interface objects, although esp_wifi_config_80211_tx is documented for use
 * before START. Keep its live PHY source when present, otherwise read the
 * configured protocols through the SDK. No RF start or rate write occurs here.
 * The patched caller preserves the native writer and non-LR validation.
 * Success packs (validation selector << 3) | native PHY into 9..31; ESP errors
 * are -1 or >= 0x100. Selectors 1/2 use the SDK band validators; selector 3 is
 * reserved for LR, fully checked below, which the SDK wrongly subjects to the
 * legacy rate <= 15 check after recognizing its rate as 41/42. This is only a
 * validator branch selector, never a selected RF band or a changed rate. */
extern unsigned char g_ic[];
extern int chm_get_current_band(void);

static int wifi_tx_rate_protocol_phy(unsigned protocols, int band)
{
    /* Inverse of the pinned wifi_get_protocol_process mapping. Unknown or
     * disabled combinations remain errors; never infer a permissive default. */
    if (band == 1) {
        bool lr = (protocols & WIFI_PROTOCOL_LR) != 0;
        switch (protocols & ~WIFI_PROTOCOL_LR) {
        case 0: return lr ? 4 : ESP_ERR_INVALID_STATE;
        case WIFI_PROTOCOL_11B: return 1;
        case WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G: return 2;
        case WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N: return 3;
#if CONFIG_SOC_WIFI_HE_SUPPORT
        case WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N | WIFI_PROTOCOL_11AX: return 7;
#endif
        default: return ESP_ERR_INVALID_STATE;
        }
    }
#if CONFIG_SOC_WIFI_SUPPORT_5G
    if (band == 2) {
        switch (protocols) {
        case WIFI_PROTOCOL_11A: return 5;
        case WIFI_PROTOCOL_11A | WIFI_PROTOCOL_11N: return 3;
        case WIFI_PROTOCOL_11A | WIFI_PROTOCOL_11N | WIFI_PROTOCOL_11AC: return 6;
        case WIFI_PROTOCOL_11A | WIFI_PROTOCOL_11N | WIFI_PROTOCOL_11AC | WIFI_PROTOCOL_11AX: return 7;
        default: return ESP_ERR_INVALID_STATE;
        }
    }
#endif
    return ESP_ERR_INVALID_STATE;
}

static int wifi_tx_rate_lr_context(wifi_interface_t interface,
    const wifi_tx_rate_config_t *config, int band)
{
    if ((config->rate != WIFI_PHY_RATE_LORA_250K && config->rate != WIFI_PHY_RATE_LORA_500K) ||
        config->ersu || config->dcm) return ESP_ERR_INVALID_ARG;
    if (band != 0 && band != 1) return ESP_ERR_INVALID_STATE;
    /* The generic SDK validator reads g_wifi_nvs[1], whereas the old rate API
     * and protocol getter use distinct STA/AP LR flags. Read the selected
     * interface through the public getter, including when a live node exists.
     * An LR-enabled peer interface must not authorize this interface. */
#if CONFIG_SOC_WIFI_SUPPORT_5G
    wifi_protocols_t value = {0};
    esp_err_t error = esp_wifi_get_protocols(interface, &value);
    unsigned protocols = value.ghz_2g;
#else
    uint8_t value = 0;
    esp_err_t error = esp_wifi_get_protocol(interface, &value);
    unsigned protocols = value;
#endif
    if (error != ESP_OK) return error;
    if (!(protocols & WIFI_PROTOCOL_LR)) return ESP_ERR_INVALID_STATE;
    int phy = wifi_tx_rate_protocol_phy(protocols, 1);
    return phy >= 1 && phy <= 7 ? (3 << 3) | phy : ESP_ERR_INVALID_STATE;
}

int esp32qjs_wifi_tx_rate_context(wifi_interface_t interface, const wifi_tx_rate_config_t *config)
{
    if (!config || (interface != WIFI_IF_STA && interface != WIFI_IF_AP)) return ESP_ERR_INVALID_ARG;
    int band = chm_get_current_band();
    if (config->phymode == WIFI_PHY_MODE_LR) return wifi_tx_rate_lr_context(interface, config, band);
    int phy;
    const unsigned char *node;
    memcpy(&node, g_ic + (4 + (unsigned)interface) * sizeof(node), sizeof(node));
    if (node != NULL && (band == 1 || band == 2)) {
#if CONFIG_IDF_TARGET_ESP32C3
        phy = node[340];
#elif CONFIG_IDF_TARGET_ESP32S3
        phy = node[360];
#elif CONFIG_IDF_TARGET_ESP32C5
        phy = node[348];
#else
#error Unreviewed SDK TX rate PHY layout
#endif
    } else {
#if CONFIG_SOC_WIFI_SUPPORT_5G
        wifi_protocols_t value = {0};
        esp_err_t error = esp_wifi_get_protocols(interface, &value);
        if (error != ESP_OK) return error;
        if (band != 1 && band != 2) {
            /* Fresh AUTO initialization has no current RF band. B/G/LR and
             * A/VHT identify their validation band explicitly. Common PHYs
             * require one configured band or equivalent protocol contexts;
             * differing contexts need an explicitly selected band first. */
            if (config->phymode <= WIFI_PHY_MODE_11G) band = 1;
            else if (config->phymode == WIFI_PHY_MODE_11A || config->phymode == WIFI_PHY_MODE_VHT20) band = 2;
            else if (!value.ghz_5g) band = 1;
            else if (!value.ghz_2g) band = 2;
            else if (config->phymode != WIFI_PHY_MODE_HT40 &&
                wifi_tx_rate_protocol_phy(value.ghz_2g, 1) == wifi_tx_rate_protocol_phy(value.ghz_5g, 2)) band = 1;
            else return ESP_ERR_INVALID_STATE;
        }
        phy = wifi_tx_rate_protocol_phy(band == 1 ? value.ghz_2g : value.ghz_5g, band);
#else
        (void)config;
        uint8_t protocols = 0;
        esp_err_t error = esp_wifi_get_protocol(interface, &protocols);
        if (error != ESP_OK) return error;
        band = 1; /* This reviewed target has only a 2.4 GHz radio. */
        phy = wifi_tx_rate_protocol_phy(protocols, band);
#endif
    }
    return phy >= 1 && phy <= 7 ? (band << 3) | phy : ESP_ERR_INVALID_STATE;
}
#endif
