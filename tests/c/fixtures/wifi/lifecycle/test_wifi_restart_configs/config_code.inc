
/* No managed TWT generation participates in this configuration fixture. */
#if CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_IDF_TARGET_ESP32C5
static struct {unsigned unused;} s_twt_recovery;
static bool wifi_radio_twt_recovery_exact_locked(const esp32_mquickjs_wifi_radio_lifecycle_t *t) {(void)t;return false;}
static bool wifi_radio_twt_recovery_owners_locked(void) {return false;}
#endif
