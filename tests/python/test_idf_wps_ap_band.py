"""Deferred production RF callbacks and M1/M2/M2D builders, SDK/crypto injected.

Only AST-parse during implementation. The original pinned callback reproduces
the wrong 5 GHz RF attribute; patched production readers/builders must refuse
an unavailable band before allocation or crypto. This does not prove RF.
"""
import os
from pathlib import Path
import sys
import unittest
from test_wireless_control_regression import compile_run

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from patch_idf_wps import function, patch_source as patch_station
from patch_idf_wps_registrar import patch_source


class WpsAPBand(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        sdk = os.environ.get('IDF_PATH')
        if not sdk:
            raise unittest.SkipTest('Set IDF_PATH to the reviewed ESP-IDF')
        root = Path(sdk) / 'components/wpa_supplicant'
        cls.original = (root / 'src/ap/wps_hostapd.c').read_bytes()
        cls.host = patch_source('src/ap/wps_hostapd.c', cls.original).decode()
        cls.registrar = patch_source('src/wps/wps_registrar.c',
                                     (root / 'src/wps/wps_registrar.c').read_bytes()).decode()
        cls.original_station = (root / 'esp_supplicant/src/esp_wps.c').read_bytes()
        cls.station = patch_station('esp_supplicant/src/esp_wps.c', cls.original_station).decode()
        cls.enrollee = patch_station('src/wps/wps_enrollee.c',
                                    (root / 'src/wps/wps_enrollee.c').read_bytes()).decode()

    def test_original_ap_callback_misreports_five_ghz(self):
        compile_run(self, TYPES + function(self.original.decode(), 'hostapd_wps_rf_band_cb') + r'''
int main(void) {
    band=WIFI_BAND_5G;
    assert(hostapd_wps_rf_band_cb(NULL)==WPS_RF_24GHZ && !reads);
    return 0;
}
''')

    def test_patched_callback_tracks_live_band_and_preserves_read_error(self):
        code = ''.join(function(self.host, name) for name in (
            'esp32qjs_wps_ap_rf_band_read', 'hostapd_wps_rf_band_cb'))
        for dualband in (0, 1):
            with self.subTest(dualband=dualband):
                compile_run(self, f'#define CONFIG_SOC_WIFI_SUPPORT_5G {dualband}\n' + TYPES + code + r'''
int main(void) {
    int result=99;
    band=WIFI_BAND_2G;assert(hostapd_wps_rf_band_cb(NULL)==WPS_RF_24GHZ);
    band=WIFI_BAND_5G;assert(hostapd_wps_rf_band_cb(NULL)==(CONFIG_SOC_WIFI_SUPPORT_5G ? WPS_RF_50GHZ : 0));
    band=WIFI_BAND_2G;assert(hostapd_wps_rf_band_cb(NULL)==WPS_RF_24GHZ);
    sdk_error=77;assert(esp32qjs_wps_ap_rf_band_read(&result)==77 && !result);
    assert(hostapd_wps_rf_band_cb(NULL)==0);
    sdk_error=0;band=99;result=99;
    assert(esp32qjs_wps_ap_rf_band_read(&result)==ESP_ERR_INVALID_RESPONSE && !result);
    assert(hostapd_wps_rf_band_cb(NULL)==0);
    return 0;
}
''')

    def test_original_station_callback_masks_read_failure(self):
        compile_run(self, TYPES + function(self.original_station.decode(), 'wps_rf_band_cb') + r'''
int main(void) {
    sdk_error=77;band_mode=WIFI_BAND_MODE_5G_ONLY;
    assert(wps_rf_band_cb(NULL)==WPS_RF_24GHZ && reads==1);
    sdk_error=0;band_mode=99;
    assert(wps_rf_band_cb(NULL)==WPS_RF_24GHZ);
    return 0;
}
''')

    def test_station_callback_rejects_failed_unknown_and_unsupported_band_modes(self):
        code = function(self.station, 'wps_rf_band_cb')
        for dualband in (0, 1):
            with self.subTest(dualband=dualband):
                compile_run(self, f'#define CONFIG_SOC_WIFI_SUPPORT_5G {dualband}\n' + TYPES + code + r'''
int main(void) {
    band_mode=WIFI_BAND_MODE_2G_ONLY;assert(wps_rf_band_cb(NULL)==WPS_RF_24GHZ);
    band_mode=WIFI_BAND_MODE_5G_ONLY;
    assert(wps_rf_band_cb(NULL)==(CONFIG_SOC_WIFI_SUPPORT_5G ? WPS_RF_50GHZ : 0));
    band_mode=WIFI_BAND_MODE_AUTO;
    assert(wps_rf_band_cb(NULL)==(CONFIG_SOC_WIFI_SUPPORT_5G ? (WPS_RF_24GHZ|WPS_RF_50GHZ) : 0));
    sdk_error=77;assert(wps_rf_band_cb(NULL)==0);
    sdk_error=0;band_mode=99;assert(wps_rf_band_cb(NULL)==0);
    assert(reads==5);
    return 0;
}
''')

    def test_builders_use_single_band_read_and_reject_before_allocation(self):
        code = function(self.enrollee, 'wps_build_m1')
        code += ''.join(function(self.registrar, name) for name in ('wps_build_m2', 'wps_build_m2d'))
        compile_run(self, TYPES + BUILDERS + code + r'''
int main(void) {
    struct wps_context context={.rf_band_cb=callback};
    struct wps_data data={.wps=&context};
    struct wpabuf *(*builders[])(struct wps_data *)={wps_build_m1,wps_build_m2,wps_build_m2d};
    const int bands[]={0,-1,8,WPS_RF_24GHZ,WPS_RF_50GHZ,WPS_RF_24GHZ|WPS_RF_50GHZ};
    for(unsigned b=0;b<3;++b)for(unsigned i=0;i<6;++i) {
        rf_result=bands[i];callbacks=allocations=crypto=0;encoded=0;allocation_failure=false;
        struct wpabuf *msg=builders[b](&data);
        assert(callbacks==1);
        if(i<3)assert(!msg && !allocations && !crypto && !encoded);
        else {assert(msg==&message && allocations==1 && encoded==bands[i]);wpabuf_free(msg);}
    }
    context.rf_band_cb=NULL;callbacks=allocations=crypto=0;
    assert(!wps_build_m1(&data) && !wps_build_m2(&data) && !wps_build_m2d(&data) && !callbacks && !allocations && !crypto);
    context.rf_band_cb=callback;rf_result=WPS_RF_50GHZ;allocation_failure=true;
    for(unsigned b=0;b<3;++b)assert(!builders[b](&data));
    return 0;
}
''')


TYPES = r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#define ESP_OK 0
#define ESP_ERR_INVALID_RESPONSE 12
#define WPS_RF_24GHZ 1
#define WPS_RF_50GHZ 2
#define WPS_RF_60GHZ 4
typedef int wifi_band_t;
typedef int wifi_band_mode_t;
typedef int esp_err_t;
enum {WIFI_BAND_2G=1,WIFI_BAND_5G=2};
enum {WIFI_BAND_MODE_2G_ONLY=1,WIFI_BAND_MODE_5G_ONLY=2,WIFI_BAND_MODE_AUTO=3};
static int band,band_mode,sdk_error,reads;
static int esp_wifi_get_band(wifi_band_t *out){++reads;*out=band;return sdk_error;}
static int esp_wifi_get_band_mode(wifi_band_mode_t *out){++reads;*out=band_mode;return sdk_error;}
#define wpa_printf(...) ((void)0)
'''


BUILDERS = r'''
typedef uint16_t u16;
typedef uint8_t u8;
struct wpabuf {int unused;};
struct wps_device_data {int rf_bands;};
struct wps_context {int (*rf_band_cb)(void *);void *cb_ctx;struct wps_device_data dev;int ap,ap_setup_locked,config_methods;};
struct wps_data {struct wps_context *wps;int config_error,int_reg,state,pbc_in_m1,dev_password_len,multi_ap_backhaul_sta;};
enum {RECV_DONE,RECV_M3,RECV_M2D_ACK,RECV_M2,WPS_CFG_NO_ERROR,WPS_CFG_SETUP_LOCKED};
enum {WPS_CONFIG_DISPLAY=1,WPS_CONFIG_PUSHBUTTON=2,WPS_CONFIG_VIRT_PUSHBUTTON=4,WPS_CONFIG_PHY_PUSHBUTTON=8,MULTI_AP_BACKHAUL_STA=1};
static struct wpabuf message;
static int rf_result,callbacks,allocations,crypto,encoded;
static bool allocation_failure;
static int callback(void *ctx){(void)ctx;++callbacks;return rf_result;}
static struct wpabuf *wpabuf_alloc(size_t bytes){assert(bytes==1000);++allocations;return allocation_failure?NULL:&message;}
static void wpabuf_free(struct wpabuf *msg){assert(msg==&message);}
static int encode(int band){encoded=band;return 0;}
static int random_bytes(void){++crypto;return 0;}
#define random_get_bytes(...) random_bytes()
#define wpa_hexdump(...) ((void)0)
#define wps_build_version(...) 0
#define wps_build_msg_type(...) 0
#define wps_build_enrollee_nonce(...) 0
#define wps_build_registrar_nonce(...) 0
#define wps_build_uuid_r(...) 0
#define wps_build_uuid_e(...) 0
#define wps_build_mac_addr(...) 0
#define wps_build_config_methods(...) 0
#define wps_build_wps_state(...) 0
#define wps_build_vendor_ext_m1(...) 0
#define wps_build_public_key(...) 0
#define wps_derive_keys(...) 0
#define wps_build_auth_type_flags(...) 0
#define wps_build_encr_type_flags(...) 0
#define wps_build_conn_type_flags(...) 0
#define wps_build_config_methods_r(...) 0
#define wps_build_device_attrs(...) 0
#define wps_build_rf_bands(dev,msg,band) encode(band)
#define wps_build_assoc_state(...) 0
#define wps_build_config_error(...) 0
#define wps_build_dev_password_id(...) 0
#define wps_build_os_version(...) 0
#define wps_build_wfa_ext(...) 0
#define wps_build_authenticator(...) 0
'''
