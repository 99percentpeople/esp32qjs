
#define ESP_ERR_WIFI_NOT_CONNECT 9
#define ESP_ERR_NOT_ALLOWED 10
static wifi_band_mode_t native_band_mode;
static wifi_band_t native_band;
static uint8_t native_channel;
static bool associated,corrupt_band,corrupt_channel,deny_regulatory;
static int esp_wifi_get_band_mode(wifi_band_mode_t *mode) {*mode=native_band_mode;return sdk_step(false);}
static int esp_wifi_get_band(wifi_band_t *band) {*band=corrupt_band ? 99 : native_band;return sdk_step(false);}
static int esp_wifi_set_band_mode(wifi_band_mode_t mode) {
    native_band_mode=mode;
    if(mode!=WIFI_BAND_MODE_AUTO) {native_band=mode==WIFI_BAND_MODE_2G_ONLY ? WIFI_BAND_2G : WIFI_BAND_5G;native_channel=native_band==WIFI_BAND_2G ? 1 : 36;}
    return sdk_step(true);
}
static int esp_wifi_set_band(wifi_band_t band) {native_band=band;native_channel=band==WIFI_BAND_2G ? 1 : 36;return sdk_step(true);}
static int esp_wifi_sta_get_ap_info(wifi_ap_record_t *ap) {memset(ap,0,sizeof(*ap));int err=sdk_step(false);return err ? err : associated ? ESP_OK : ESP_ERR_WIFI_NOT_CONNECT;}
/* Existing callback-aware channel refresh has its own production fixture.
 * This is its native read boundary, not an alternate band transition model. */
static int wifi_radio_get_channel_locked(uint8_t *primary,wifi_second_chan_t *secondary,uint32_t *generation) {
    *primary=corrupt_channel ? 0 : native_channel;*secondary=WIFI_SECOND_CHAN_NONE;*generation=4;return sdk_step(false);
}
static int esp_wifi_get_country(wifi_country_t *country) {
    *country=(wifi_country_t){.cc="US",.schan=1,.nchan=11,.policy=WIFI_COUNTRY_POLICY_MANUAL};
#if CONFIG_SOC_WIFI_SUPPORT_5G
    country->wifi_5g_channel_mask=deny_regulatory ? 4 : 2; /* 40 vs 36 */
#endif
    return sdk_step(false);
}
