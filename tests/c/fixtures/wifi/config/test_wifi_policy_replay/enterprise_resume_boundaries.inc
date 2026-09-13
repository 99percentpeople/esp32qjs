
#define CONFIG_ESP_WIFI_ENTERPRISE_SUPPORT 1
static unsigned enterprise_installs;static int enterprise_error;
static int wifi_radio_eap_restart_install_locked(struct esp32_mquickjs_wifi_eap_profile *profile,
    const esp32_mquickjs_wifi_radio_lease_t owners[3]) {
    assert(locks==1&&!critical&&profile==(void *)1&&config_committed&&!config_discards);
    assert(s_radio.lifecycle.identity&&s_radio.started&&!observed_owner->acquired);
    assert(owners[0].acquired&&owners[1].acquired&&!owners[2].acquired);
    ++enterprise_installs;return enterprise_error;
}
