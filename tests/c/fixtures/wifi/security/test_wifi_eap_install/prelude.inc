
#define CONFIG_ESP_WIFI_SUITE_B_192 1
#define ESP_ERR_INVALID_STATE 5
#define ESP_FAIL -1
static bool on_wifi,on_worker,dispatch_fail,foreign,clear_fail,dirty,okc,time_disabled;
static bool activation_attempted;
static unsigned steps,fail_at,clears,commits,dispatches;
static uint32_t resources;
static int observed_cleanup_error,observed_control_error;
static const unsigned char *borrowed_ca,*borrowed_cert,*borrowed_key;
static int ca_length,cert_length,key_length;
static unsigned seen_methods,seen_phase2,seen_fast,seen_pac;
static bool seen_bundle,seen_suiteb;
static unsigned setter(void) {
    assert(on_wifi && !critical);steps++;
    return fail_at && steps==fail_at ? 71 : 0;
}
