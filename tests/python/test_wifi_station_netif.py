"""SDK boundary failures in production Station netif prepare/retire helpers."""
import pathlib
import unittest
from test_wireless_control_regression import function, compile_run

ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'components/esp32_mquickjs/src/modules/wifi/esp32_mquickjs_wifi.c'


class WiFiStationNetif(unittest.TestCase):
    def test_prepare_failures_unwind_and_failed_detach_is_never_retried(self):
        source = SOURCE.read_text()
        body = function(source, 'wifi_prepare_station_netif') + function(source, 'wifi_retire_station_netif')
        compile_run(self, r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_STATE 0x103
typedef struct { int unused; } esp_netif_t;
typedef struct { int unused; } esp_netif_config_t;
#define ESP_NETIF_DEFAULT_WIFI_STA() ((esp_netif_config_t){0})
static struct { esp_netif_t *sta_netif; int sta_detach_error; } s_wifi_state;
static const char *s_wifi_setup_stage;
static esp_netif_t netif_object;
static int failed, calls[5], destroyed;
static esp_netif_t *esp_netif_new(const esp_netif_config_t *cfg) {
    assert(cfg);calls[1]++;return failed==1?NULL:&netif_object;
}
static int esp_netif_attach_wifi_station(esp_netif_t *netif) {
    assert(netif==&netif_object);calls[2]++;return failed==2?-2:0;
}
static int esp_wifi_set_default_wifi_sta_handlers(void) {
    calls[3]++;return failed==3?-3:0;
}
static int esp_wifi_clear_default_wifi_driver_and_handlers(esp_netif_t *netif) {
    assert(netif==&netif_object);calls[4]++;return failed==4?-4:0;
}
static void esp_netif_destroy(esp_netif_t *netif) { assert(netif==&netif_object);destroyed++; }
/* SDK-facing retirement boundary; its scheduler is covered separately by
 * test_wifi_netif_retirement using the complete production translation unit. */
static int esp32_mquickjs_wifi_netif_retire(esp_netif_t **n,int *error) {
    if(*error)return *error;
    if(!*n)return ESP_OK;
    *error=esp_wifi_clear_default_wifi_driver_and_handlers(*n);
    if(!*error){esp_netif_destroy(*n);*n=NULL;}
    return *error;
}
''' + body + r'''
int main(void) {
    for(int stage=1;stage<=3;stage++) {
        memset(&s_wifi_state,0,sizeof(s_wifi_state));memset(calls,0,sizeof(calls));destroyed=0;
        failed=stage;
        assert(wifi_prepare_station_netif()==(stage==1?ESP_ERR_NO_MEM:-stage));
        assert(!strcmp(s_wifi_setup_stage,stage==1?"netif-create":stage==2?"netif-attach":"netif-handlers"));
        for(int step=1;step<=3;step++) assert(calls[step]==(step<=stage));
        failed=0;assert(wifi_retire_station_netif()==ESP_OK);
        assert(!s_wifi_state.sta_netif && calls[4]==(stage>1) && destroyed==(stage>1));
        assert(wifi_retire_station_netif()==ESP_OK && calls[4]==(stage>1));
    }
    memset(&s_wifi_state,0,sizeof(s_wifi_state));memset(calls,0,sizeof(calls));destroyed=0;
    assert(wifi_prepare_station_netif()==ESP_OK);
    failed=4;assert(wifi_retire_station_netif()==-4);
    assert(s_wifi_state.sta_netif && s_wifi_state.sta_detach_error==-4 && !destroyed);
    failed=0;assert(wifi_retire_station_netif()==-4 && calls[4]==1 && !destroyed);
    assert(wifi_prepare_station_netif()==ESP_ERR_INVALID_STATE && calls[1]==1);
    return 0;
}
''')
