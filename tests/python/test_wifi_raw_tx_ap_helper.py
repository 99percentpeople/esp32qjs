"""Deferred production AP helper coordinator with actual Radio rate handoff."""
import unittest
from test_wifi_raw_tx_ap_rate import ap_rate_code, MAIN as NATIVE_MAIN
from test_wifi_tx_rate import COMPONENT
from test_wifi_config_controls import structure
from test_wireless_control_regression import compile_run
from wireless_vm_fixture import extract


class WiFiRawTxApHelper(unittest.TestCase):
    def test_helper_partial_open_close_suffix_and_recovery_replacement(self):
        for profile in ('esp32c3/representative','esp32s3/representative-psram','esp32c5/representative'):
            code = ap_rate_code(profile)
            code += structure((COMPONENT/'internal/esp32_mquickjs_wifi_raw_tx_ap.h').read_text(),
                              'esp32_mquickjs_wifi_raw_tx_ap_context_t')
            code += BOUNDARIES
            source = (COMPONENT/'src/modules/wifi/esp32_mquickjs_wifi_ap.c').read_text()
            for name in ('esp32_mquickjs_wifi_ap_open_raw_tx_rate','esp32_mquickjs_wifi_ap_close_raw_tx_rate'):
                code += extract(source,name)
            code += NATIVE_MAIN[:NATIVE_MAIN.index('int main(void)')]
            with self.subTest(profile=profile):compile_run(self,code+MAIN)


BOUNDARIES = r'''
static void *s_ap_netif;
static esp32_mquickjs_wifi_radio_lease_t s_ap_lease;
static esp32_mquickjs_wifi_radio_lifecycle_t s_ap_lifecycle,s_ap_coordinator;
static struct {uint32_t generation,identity;} s_ap_raw_owner;
static bool s_ap_cleanup_pending,helper_ready=true;
static const char *s_ap_stage;
static esp_err_t s_ap_detach_error,prepare_error,retire_error;
static unsigned prepares,retires,finishes;
static bool station_live;
static unsigned station_prepares,station_retires;
static esp_err_t station_prepare_error,station_retire_error;
static bool esp32_mquickjs_wifi_raw_tx_ap_ready(void) {return helper_ready;}
static esp_err_t esp32_mquickjs_wifi_retire_for_configuration(const esp32_mquickjs_wifi_radio_lifecycle_t *t) {
    esp_err_t err=esp32_mquickjs_wifi_radio_check_stopped_lifecycle(t,true);if(err)return err;
    ++station_retires;if(station_retire_error)return station_retire_error;
    station_live=false;return ESP_OK;
}
static esp_err_t esp32_mquickjs_wifi_prepare_for_configuration(const esp32_mquickjs_wifi_radio_lifecycle_t *t) {
    esp_err_t err=esp32_mquickjs_wifi_radio_check_stopped_lifecycle(t,false);if(err)return err;
    assert(!station_live);++station_prepares;station_live=true;return station_prepare_error;
}
static esp_err_t wifi_ap_prepare_netif(void) {
    assert(!locks && !critical && !s_ap_netif && s_radio.lifecycle.identity);++prepares;
    s_ap_netif=(void *)1;return prepare_error;
}
static esp_err_t wifi_ap_retire_netif(void) {
    assert(!locks && !critical && !s_radio.started && !s_radio.stop_required);++retires;
    if(retire_error)return retire_error;s_ap_netif=NULL;return ESP_OK;
}
/* Final shared lifecycle release is covered by its own production fixture. */
static esp_err_t esp32_mquickjs_wifi_radio_finish_lifecycle(esp32_mquickjs_wifi_radio_lifecycle_t *t,bool shutdown) {
    assert(!shutdown && !s_ap_netif && !station_live && !s_tx_rate_lease.identity);++finishes;
    int err=esp32_mquickjs_wifi_radio_check_stopped_lifecycle(t,true);if(err)return err;
    memset(&s_radio.lifecycle,0,sizeof(s_radio.lifecycle));memset(t,0,sizeof(*t));return ESP_OK;
}
'''

MAIN = r'''
int main(void) {
    (void)begin;(void)start;(void)close_rate;(void)esp32_mquickjs_wifi_radio_5ghz_channel_bit;
    esp32_mquickjs_wifi_raw_tx_ap_context_t context={0};const char *stage;
    setup();assert(!esp32_mquickjs_wifi_ap_open_raw_tx_rate(&context,&requested,6,&lease,&channel,&stage));
    assert(context.owned && !context.lifecycle.identity && s_ap_raw_owner.identity==lease.identity && s_ap_netif);
    assert(!stage && !s_ap_cleanup_pending && allocations==frees);
    retire_error=61;
    assert(esp32_mquickjs_wifi_ap_close_raw_tx_rate(&context,&lease,false,&stage)==61);
    assert(context.owned && context.lifecycle.identity && !lease.acquired && s_ap_cleanup_pending && s_ap_netif);
    unsigned previous_writes=writes,previous_stops=stops;retire_error=0;
    assert(!esp32_mquickjs_wifi_ap_close_raw_tx_rate(&context,&lease,false,&stage));
    assert(!context.owned && !s_ap_raw_owner.identity && !s_ap_netif && !s_ap_coordinator.identity);
    assert(writes==previous_writes && stops==previous_stops);
    setup();prepare_error=62;
    assert(esp32_mquickjs_wifi_ap_open_raw_tx_rate(&context,&requested,6,&lease,&channel,&stage)==62);
    assert(context.owned && context.lifecycle.identity && s_ap_netif && !writes && !starts);
    prepare_error=0;assert(!esp32_mquickjs_wifi_ap_close_raw_tx_rate(&context,&lease,false,&stage));
    setup();unsigned prepared=prepares;
    assert(esp32_mquickjs_wifi_ap_open_raw_tx_rate(&context,&requested,11,&lease,&channel,&stage)==ESP_ERR_INVALID_ARG);
    assert(context.owned && prepares==prepared && !writes && !starts);
    assert(!esp32_mquickjs_wifi_ap_close_raw_tx_rate(&context,&lease,false,&stage));
    setup();s_radio.effective_mode=WIFI_MODE_APSTA;
    assert(!esp32_mquickjs_wifi_ap_open_raw_tx_rate(&context,&requested,6,&lease,&channel,&stage));
    assert(context.mode==WIFI_MODE_APSTA && context.station_cleanup_pending && station_live && s_radio.effective_mode==WIFI_MODE_APSTA);
    station_retire_error=63;unsigned written=writes;
    assert(esp32_mquickjs_wifi_ap_close_raw_tx_rate(&context,&lease,false,&stage)==63);
    assert(context.owned && context.lifecycle.identity && !s_ap_netif && station_live && context.station_cleanup_pending);
    unsigned restored=writes;assert(restored>written);station_retire_error=0;
    assert(!esp32_mquickjs_wifi_ap_close_raw_tx_rate(&context,&lease,false,&stage));
    assert(!station_live && !context.owned && writes==restored && s_radio.effective_mode==WIFI_MODE_APSTA);
    setup();s_radio.effective_mode=WIFI_MODE_APSTA;station_prepare_error=64;
    assert(esp32_mquickjs_wifi_ap_open_raw_tx_rate(&context,&requested,6,&lease,&channel,&stage)==64);
    assert(context.station_cleanup_pending && station_live && !s_ap_netif && !writes && !starts);
    station_prepare_error=0;assert(!esp32_mquickjs_wifi_ap_close_raw_tx_rate(&context,&lease,false,&stage));
    assert(!station_live && !context.owned);
    /* An old Station helper retirement can fail even for an AP-only source.
     * It must retain the exclusion token until that cleanup also completes. */
    setup();station_live=true;station_retire_error=65;
    assert(esp32_mquickjs_wifi_ap_open_raw_tx_rate(&context,&requested,6,&lease,&channel,&stage)==65);
    assert(context.owned && context.station_cleanup_pending && !writes && !starts);
    assert(esp32_mquickjs_wifi_ap_close_raw_tx_rate(&context,&lease,false,&stage)==65 && context.owned);
    station_retire_error=0;assert(!esp32_mquickjs_wifi_ap_close_raw_tx_rate(&context,&lease,false,&stage));
    assert(!context.owned && !station_live);
    setup();s_radio.effective_mode=WIFI_MODE_APSTA;
    assert(!esp32_mquickjs_wifi_ap_open_raw_tx_rate(&context,&requested,0,&lease,&channel,&stage));
    assert(esp32_mquickjs_wifi_ap_close_raw_tx_rate(&context,&lease,true,&stage)==ESP_ERR_INVALID_STATE);
    /* Inject central recovery's already-proven helper retirement and original
     * Radio owner release; a subsequently rebuilt helper must remain untouched. */
    memset(&lease,0,sizeof(lease));memset(&s_ap_raw_owner,0,sizeof(s_ap_raw_owner));
    s_ap_netif=(void *)2;station_live=true;unsigned retired=retires,station_retired=station_retires;
    assert(!esp32_mquickjs_wifi_ap_close_raw_tx_rate(&context,&lease,true,&stage));
    assert(!context.owned && s_ap_netif==(void *)2 && retires==retired && station_live && station_retires==station_retired);
    return 0;
}
'''
