
#define ESP_ERR_NO_MEM 17
#define WIFI_RADIO_SMARTCONFIG_PENDING false
#define WIFI_RADIO_WPS_PENDING false
#define WIFI_RADIO_DPP_PENDING false
#define WIFI_RADIO_EAP_PENDING false
#define WIFI_RADIO_NAN_PENDING false
#define WIFI_RADIO_MESH_PENDING false
static unsigned native_starts,native_stops;
static bool native_running,band_cycle_pending;
static int wifi_radio_begin_events(int phase,wifi_mode_t mode) {
    assert((phase==RADIO_EVENTS_START && mode!=WIFI_MODE_NULL && mode==native_mode && !s_radio.started) ||
        (phase==RADIO_EVENTS_RESTART && mode==WIFI_MODE_STA && s_radio.started && !band_cycle_pending));
    return sdk_step(false);
}
static int wifi_radio_wait_events(void) {
    assert(s_radio.started && s_radio.stop_required);int err=sdk_step(false);
    if(err==ESP_OK)band_cycle_pending=false;return err;
}
static int esp_wifi_start(void);
#define WIFI_PROTOCOL_11B 1
#define WIFI_PROTOCOL_11G 2
#define WIFI_PROTOCOL_11N 4
#define WIFI_PROTOCOL_LR 8
#define WIFI_PROTOCOL_11A 16
#define WIFI_PROTOCOL_11AC 32
#define WIFI_PROTOCOL_11AX 64
static wifi_band_mode_t native_band_mode;
static wifi_band_t native_band;
static uint8_t native_channel,native_home;
static wifi_second_chan_t native_secondary,native_home_secondary;
static unsigned band_writes,channel_writes,phy_reads[2];
static bool change_visible_phy,ignore_channel;
static int esp_wifi_get_band(wifi_band_t *out) {*out=native_band;return sdk_step(false);}
static int esp_wifi_get_channel(uint8_t *out,wifi_second_chan_t *secondary) {
    *out=native_channel;*secondary=native_secondary;return sdk_step(false);
}
static int esp_wifi_get_home_channel(uint8_t *out,wifi_second_chan_t *secondary) {
    *out=native_home;*secondary=native_home_secondary;return sdk_step(false);
}
static wifi_protocols_t native_protocols[2];
static wifi_bandwidths_t native_bandwidths[2];
static int esp_wifi_get_band_mode(wifi_band_mode_t *mode) {*mode=native_band_mode;return sdk_step(false);}
#if CONFIG_SOC_WIFI_SUPPORT_5G
static int esp_wifi_get_protocols(wifi_interface_t iface,wifi_protocols_t *out) {
    ++phy_reads[iface];*out=native_protocols[iface];
    if(native_band_mode==WIFI_BAND_MODE_2G_ONLY)out->ghz_5g=0;
    if(native_band_mode==WIFI_BAND_MODE_5G_ONLY)out->ghz_2g=0;
    return sdk_step(false);
}
static int esp_wifi_get_bandwidths(wifi_interface_t iface,wifi_bandwidths_t *out) {
    *out=native_bandwidths[iface];
    if(native_band_mode==WIFI_BAND_MODE_2G_ONLY)out->ghz_5g=0;
    if(native_band_mode==WIFI_BAND_MODE_5G_ONLY)out->ghz_2g=0;
    return sdk_step(false);
}
static int esp_wifi_set_protocols(wifi_interface_t iface,wifi_protocols_t *input) {
    assert(!s_radio.started && native_band_mode==WIFI_BAND_MODE_AUTO);
    assert(native_bandwidths[iface].ghz_2g==WIFI_BW20 && native_bandwidths[iface].ghz_5g==WIFI_BW20);
    native_protocols[iface]=*input;input->ghz_2g=0;return sdk_step(true);
}
static int esp_wifi_set_bandwidths(wifi_interface_t iface,wifi_bandwidths_t *input) {
    assert(!s_radio.started && native_band_mode==WIFI_BAND_MODE_AUTO);
    native_bandwidths[iface]=*input;input->ghz_2g=(wifi_bandwidth_t)99;return sdk_step(true);
}
#else
static int esp_wifi_get_protocol(wifi_interface_t iface,uint8_t *out) {*out=native_protocols[iface].ghz_2g;return sdk_step(false);}
static int esp_wifi_get_bandwidth(wifi_interface_t iface,wifi_bandwidth_t *out) {*out=native_bandwidths[iface].ghz_2g;return sdk_step(false);}
static int esp_wifi_set_protocol(wifi_interface_t iface,uint8_t input) {
    assert(!s_radio.started && native_bandwidths[iface].ghz_2g==WIFI_BW20);
    native_protocols[iface].ghz_2g=input;return sdk_step(true);
}
static int esp_wifi_set_bandwidth(wifi_interface_t iface,wifi_bandwidth_t input) {
    assert(!s_radio.started);native_bandwidths[iface].ghz_2g=input;return sdk_step(true);
}
#endif
#define WIFI_EVENT_MASK_AP_PROBEREQRECVED 1U
static wifi_country_t native_country;
static wifi_ps_type_t native_ps;
static uint32_t native_event_mask;
static uint8_t native_mac[2][6];
static const uint8_t factory_mac[2][6]={{2,0,0,0,0,11},{2,0,0,0,0,12}};
static unsigned mac_writes,power_writes,rate_writes[2];
static esp32_mquickjs_wifi_tx_rate_state_t s_tx_rates;
static wifi_tx_rate_config_t native_rates[2];
static int esp_wifi_config_80211_tx(wifi_interface_t iface,wifi_tx_rate_config_t *config) {
    assert(!s_radio.started && (iface==WIFI_IF_STA || (CONFIG_ESP_WIFI_SOFTAP_SUPPORT && iface==WIFI_IF_AP)));
    native_rates[iface]=*config;++rate_writes[iface];
    config->rate=WIFI_PHY_RATE_1M_L; /* Production SDK boundary preserves frozen and accepted input. */
    return sdk_step(true);
}
static esp32_mquickjs_wifi_interval_state_t s_interval;
static uint16_t native_interval;
static int wifi_radio_interval_writer(void *opaque,uint16_t value) {
    (void)opaque;native_interval=value;return sdk_step(true);
}
static int8_t native_tx_power;
static bool clamp_tx_power;
static int esp_wifi_get_max_tx_power(int8_t *output) {
    int err=sdk_step(false);if(err!=ESP_OK)return err;
    if(!s_radio.started)return ESP_ERR_WIFI_NOT_STARTED;
    *output=native_tx_power;return ESP_OK;
}
static int esp_wifi_set_max_tx_power(int8_t input) {
    assert(s_radio.started && !critical);++power_writes;
    native_tx_power=clamp_tx_power ? input-1 : input;return sdk_step(true);
}
static int esp_wifi_get_country(wifi_country_t *output) {*output=native_country;return sdk_step(false);}
static int esp_wifi_set_country(const wifi_country_t *input) {
    assert(!s_radio.started);int8_t maximum=native_country.max_tx_power;
    native_country=*input;native_country.max_tx_power=maximum;return sdk_step(true);
}
static int esp_wifi_get_ps(wifi_ps_type_t *output) {*output=native_ps;return sdk_step(false);}
static int esp_wifi_set_ps(wifi_ps_type_t input) {assert(!s_radio.started);native_ps=input;return sdk_step(true);}
static int esp_wifi_get_event_mask(uint32_t *output) {*output=native_event_mask;return sdk_step(false);}
static int esp_wifi_set_event_mask(uint32_t input) {
    assert(!s_radio.started && !(input & ~WIFI_EVENT_MASK_AP_PROBEREQRECVED));
    native_event_mask=input;return sdk_step(true);
}
static int esp_wifi_get_mac(wifi_interface_t iface,uint8_t *output) {
    assert(iface==WIFI_IF_STA || (CONFIG_ESP_WIFI_SOFTAP_SUPPORT && iface==WIFI_IF_AP));
    memcpy(output,native_mac[iface],6);return sdk_step(false);
}

static struct {esp32_mquickjs_wifi_radio_lifecycle_t owner;} s_policy_restart;
static wifi_config_t native_config[2];
static bool reenable_pmf;
static wifi_storage_t native_storage;
static int esp_wifi_set_channel(uint8_t primary,wifi_second_chan_t secondary) {
    assert(s_radio.started && native_mode==WIFI_MODE_STA && !band_cycle_pending);
    ++channel_writes;
    if(!ignore_channel) {
        native_channel=native_home=primary;native_secondary=native_home_secondary=secondary;
        native_band=primary>14 ? WIFI_BAND_5G : WIFI_BAND_2G;
    }
    return sdk_step(true);
}
#if CONFIG_SOC_WIFI_SUPPORT_5G
static int esp_wifi_set_band_mode(wifi_band_mode_t mode) {
    assert(s_radio.started && s_radio.driver_state==ESP32_MQUICKJS_WIFI_RADIO_STARTING && native_running);
    assert((mode==WIFI_BAND_MODE_AUTO || mode==WIFI_BAND_MODE_2G_ONLY || mode==WIFI_BAND_MODE_5G_ONLY) && native_mode==WIFI_MODE_STA);
    assert(native_storage==WIFI_STORAGE_RAM);
    native_band_mode=mode;band_cycle_pending=true;++band_writes;
    native_band=mode==WIFI_BAND_MODE_5G_ONLY ? WIFI_BAND_5G : WIFI_BAND_2G;
    native_channel=native_home=mode==WIFI_BAND_MODE_5G_ONLY ? 36 : 1;
    if(change_visible_phy)native_protocols[0].ghz_2g=WIFI_PROTOCOL_11B;
    return sdk_step(true);
}
#endif
static int esp_wifi_set_mac(wifi_interface_t iface,const uint8_t *input) {
    assert(!s_radio.started && !(input[0]&1U));
    /* Fixed SDK wifi_set_mac_process requires the selected mode to contain
     * the interface, even while its physical START flag is clear. */
    if(!(native_mode & (1U<<iface)))return 0x3005; /* ESP_ERR_WIFI_MODE */
    assert(iface==WIFI_IF_STA || (CONFIG_ESP_WIFI_SOFTAP_SUPPORT && iface==WIFI_IF_AP));
    if(CONFIG_ESP_WIFI_SOFTAP_SUPPORT)assert(memcmp(input,native_mac[1-iface],6));
    memcpy(native_mac[iface],input,6);++mac_writes;return sdk_step(true);
}

static void *allocation;static size_t allocation_size;static unsigned allocations,frees,nvs_writes;
static bool allocation_fail,mutate_input,corrupt_config;
static int cleanup_error;
static int esp_wifi_start(void) {
    assert((native_mode==WIFI_MODE_STA || (CONFIG_ESP_WIFI_SOFTAP_SUPPORT && (native_mode & WIFI_MODE_AP))) && s_radio.lifecycle.identity==41 && allocation);
    if((native_mode & WIFI_MODE_AP) && native_storage==WIFI_STORAGE_FLASH)++nvs_writes;
    for(unsigned i=0;i<WIFI_RADIO_MAX_LEASES;++i)assert(!s_radio.leases[i].identity);
    native_running=true;++native_starts;return sdk_step(true);
}

static void *checkpoint_calloc(size_t n,size_t size) {
    assert(locks==1 && !critical && !allocation);++allocations;if(allocation_fail)return NULL;
    allocation_size=n*size;allocation=calloc(n,size);assert(allocation);return allocation;
}
static void checkpoint_free(void *p) {
    assert(p==allocation && !critical);const unsigned char *bytes=p;
    for(size_t i=0;i<allocation_size;++i)assert(bytes[i]==0); /* Every retained secret was wiped. */
    ++frees;free(p);allocation=NULL;
}
#define calloc checkpoint_calloc
#define free checkpoint_free
static int esp_wifi_get_mode(wifi_mode_t *mode) {*mode=native_mode;return sdk_step(false);}
static int esp_wifi_set_mode(wifi_mode_t mode) {assert(!s_radio.started);native_mode=mode;return sdk_step(true);}
static int esp_wifi_set_storage(wifi_storage_t storage) {native_storage=storage;return sdk_step(true);}
static int esp_wifi_get_config(wifi_interface_t iface,wifi_config_t *output) {
    *output=native_config[iface];if(corrupt_config && iface==WIFI_IF_STA)output->sta.pmf_cfg.required=!output->sta.pmf_cfg.required;
    return sdk_step(false); /* Partial output can exist before SDK failure. */
}
static int esp_wifi_disable_pmf_config(wifi_interface_t iface) {
    assert(!s_radio.started);
    wifi_pmf_config_t *pmf=iface==WIFI_IF_STA ? &native_config[iface].sta.pmf_cfg : &native_config[iface].ap.pmf_cfg;
    pmf->capable=pmf->required=false;return sdk_step(true);
}
static int esp_wifi_set_config(wifi_interface_t iface,wifi_config_t *input) {
    assert(!s_radio.started && (native_mode & (iface==WIFI_IF_STA ? WIFI_MODE_STA : WIFI_MODE_AP)));
    if(native_storage==WIFI_STORAGE_FLASH)++nvs_writes;
    native_config[iface]=*input;
    if(reenable_pmf) {
        wifi_pmf_config_t *pmf=iface==WIFI_IF_STA ? &native_config[iface].sta.pmf_cfg : &native_config[iface].ap.pmf_cfg;
        pmf->capable=true; /* capable=false alone is not a disable request. */
    }
    if(mutate_input)input->sta.password[0]^=1; /* SDK cannot corrupt frozen original. */
    return sdk_step(true);
}
static int wifi_radio_shutdown_locked(void) {assert(locks==1);return cleanup_error;}
static int wifi_radio_stop_locked(void) {
    if(!s_radio.stop_required)return ESP_OK;
    assert(locks==1 && native_running && !band_cycle_pending);++native_stops;
    int err=cleanup_error ? cleanup_error : sdk_step(true);
    if(err==ESP_OK){native_running=false;s_radio.started=s_radio.stop_required=false;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STOPPED;}
    return err;
}
