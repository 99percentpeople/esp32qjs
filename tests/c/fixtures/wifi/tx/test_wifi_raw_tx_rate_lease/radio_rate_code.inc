
static struct {bool stopped;} s_raw_tx_recovery;
static bool wifi_radio_raw_tx_recovery_exact_locked(const void *t) {(void)t;return false;}
static bool wifi_radio_raw_tx_recovery_owner_locked(const esp32_mquickjs_wifi_radio_lease_t *l) {(void)l;return false;}
