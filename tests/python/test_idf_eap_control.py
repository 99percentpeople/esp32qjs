"""Deferred production EAP control and deattach failure propagation.

SDK driver writes/register storage, method allocation and eloop are boundaries;
control admission, callback installation, public dispatch, suffix cleanup and
native diagnostics execute the actual composed SDK bodies.
"""
import os
from pathlib import Path
import sys
import unittest
from test_wireless_control_regression import compile_run
from wireless_vm_fixture import extract
from test_wifi_rx_target import ROOT, unit
sys.path.insert(0, str(ROOT / 'scripts'))
from patch_idf_eap_control import patch_source


class IDFEAPControl(unittest.TestCase):
    def test_control_failures_identity_of_steps_and_wifi_task_dispatch(self):
        if not os.environ.get('IDF_PATH'):
            self.skipTest('Set IDF_PATH to the reviewed ESP-IDF')
        component = Path(os.environ['IDF_PATH']) / 'components/wpa_supplicant'
        relative = 'esp_supplicant/src/esp_eap_client.c'
        source = patch_source(relative, (component / relative).read_bytes()).decode()
        main_relative = 'esp_supplicant/src/esp_wpa_main.c'
        main = patch_source(main_relative, (component / main_relative).read_bytes()).decode()
        code = PRELUDE + unit(component / 'esp_supplicant/include/esp_eap_client.h')
        driver = (component / 'esp_supplicant/src/esp_wifi_driver.h').read_text()
        for anchor in ['struct wpa2_funcs {', 'typedef esp_err_t (*wifi_wpa2_fn_t)']:
            start = driver.index(anchor)
            end = driver.index('};', start)+3 if anchor.startswith('struct') else driver.index('} wifi_wpa2_param_t;',start)+len('} wifi_wpa2_param_t;')
            code += driver[start:end]+'\n'
        start = source.index('static void *s_wpa2_task_hdl')
        code += source[start:source.index('static void config_changed_handler', start)]
        peer = (component / 'src/eap_peer/eap.c').read_text()
        code += peer[peer.index('u8 *g_wpa_anonymous_identity;'):peer.index('void eap_peer_config_deinit')]
        code += BOUNDARIES
        for name in ['wpa2_api_lock','wpa2_api_unlock','wpa2_is_enabled','wpa2_set_state',
                     'esp_client_enable_fn','esp32qjs_eap_enable_dispatch','esp_wifi_sta_enterprise_enable',
                     'eap_client_disable_fn','esp32qjs_eap_disable_dispatch','esp_wifi_sta_enterprise_disable',
                     'esp32qjs_eap_native_resources','esp32qjs_eap_native_control_error',
                     'esp32qjs_eap_on_worker','esp32qjs_eap_configuration_idle']:
            code += extract(source, name)
        code += extract(main, 'wpa_deattach')
        compile_run(self, code + MAIN)


PRELUDE = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
typedef uint8_t u8;
typedef uint32_t u32;
typedef int esp_err_t;
#define CONFIG_ESP_WIFI_ENTERPRISE_SUPPORT 1
#define CONFIG_ESP_WIFI_ENABLE_ROAMING_APP 1
#define EAP_PEER_METHOD 1
#define TRUE 1
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM -2
#define ESP_ERR_INVALID_STATE -3
#define ESP_ERR_INVALID_ARG -4
#define wpa_printf(...) ((void)0)
typedef enum {WPA2_STATE_DISABLED,WPA2_STATE_ENABLED} wpa2_state_t;
struct eap_sm {int unused;};
static struct eap_sm *gEapSm;
static void *s_wpa2_data_lock;
static bool g_wpa_config_changed;
struct wpa_sm {int (*wpa_sm_eap_disable)(void);int (*wpa_sm_wps_disable)(void);};
static struct wpa_sm gWpaSm;
'''

BOUNDARIES = r'''
static bool on_wifi,on_eap,api_locked,fail_alloc,fail_lock,fail_dispatch,fail_methods,fail_retire,adopt_on_error;
static int register_error,unregister_error,driver_error;
static bool driver_enabled;
static struct wpa2_funcs *driver_callbacks;
static unsigned live,allocations,dispatches,enables,disables,registers,unregisters,methods,method_clears,global_clears;
static unsigned sae_frees,wps_frees,wpa_frees,roam_frees;
static int eap_task_marker;
bool current_task_is_wifi_task(void){return on_wifi;}
static void *os_task_get_current_task(void){return on_eap?&eap_task_marker:NULL;}
static void *os_zalloc(size_t n){assert(on_wifi);allocations++;if(fail_alloc){fail_alloc=false;return NULL;}void *p=calloc(1,n);assert(p);live++;return p;}
static void os_free(void *p){if(p){live--;free(p);}}
static void *os_recursive_mutex_create(void){assert(on_wifi);return os_zalloc(1);}
static int os_mutex_lock(void *p){assert(on_wifi && p && !api_locked);if(fail_lock)return 0;api_locked=true;return TRUE;}
static void os_mutex_unlock(void *p){assert(on_wifi && p && api_locked);api_locked=false;}
static int eloop_register_timeout_blocking(int (*fn)(void *,void *),void *a,void *b){
    assert(!on_wifi && !api_locked);dispatches++;if(fail_dispatch)return -1;
    on_wifi=true;int result=fn(a,b);on_wifi=false;assert(!api_locked);return result;
}
static int esp_wifi_sta_wpa2_ent_enable_internal(wifi_wpa2_param_t *p){assert(on_wifi && api_locked);enables++;if(driver_error)return driver_error;driver_enabled=true;return p->fn(p->param);}
static int esp_wifi_sta_wpa2_ent_disable_internal(wifi_wpa2_param_t *p){assert(on_wifi && api_locked);disables++;if(driver_error)return driver_error;driver_enabled=false;return p->fn(p->param);}
static int esp_wifi_register_wpa2_cb_internal(struct wpa2_funcs *p){assert(on_wifi && api_locked);registers++;if(driver_callbacks)os_free(driver_callbacks);driver_callbacks=NULL;if(!register_error || adopt_on_error)driver_callbacks=p;return register_error;}
static int esp_wifi_unregister_wpa2_cb_internal(void){assert(on_wifi && api_locked);unregisters++;if(unregister_error)return unregister_error;os_free(driver_callbacks);driver_callbacks=NULL;return 0;}
static int eap_peer_register_methods(void){assert(on_wifi && api_locked);methods=1;return fail_methods?-1:0;}
static void eap_peer_unregister_methods(void){assert(on_wifi && api_locked);methods=0;method_clears++;}
static int wpa2_ent_rx_eapol(u8 *a,u8 *b,u32 n,u8 *c){(void)a;(void)b;(void)n;(void)c;return 0;}
static int eap_start_eapol_timer(void){return 0;}
static int eap_peer_sm_init(void){return 0;}
static void eap_peer_sm_deinit(void){assert(on_wifi && api_locked);if(fail_retire){esp32qjs_eap_retiring=true;esp32qjs_eap_cleanup_error=77;return;}gEapSm=NULL;esp32qjs_eap_retiring=false;esp32qjs_eap_cleanup_error=0;}
static void eap_globals_reset(void){assert(on_wifi && api_locked && !gEapSm && !driver_callbacks);global_clears++;g_wpa_password=NULL;}
void esp_wifi_set_okc_support(bool value){assert(on_wifi && api_locked && value);}
static void esp_wpa3_free_sae_data(void){sae_frees++;}
static void roam_deinit_app(void){roam_frees++;}
static int clear_wps(void){wps_frees++;return 0;}
static int esp_wifi_register_eapol_txdonecb_internal(void *p){assert(!p);return 0;}
static void wpa_sm_deinit(void){wpa_frees++;}
'''

MAIN = r'''
static void clean(void) {
    assert(esp_wifi_sta_enterprise_disable()==ESP_OK);
    assert(!driver_enabled && !driver_callbacks && !methods && !gWpaSm.wpa_sm_eap_disable && !esp32qjs_eap_native_resources());
}
int main(void) {
    assert(!esp32qjs_eap_configuration_idle());
    on_wifi=true;assert(esp32qjs_eap_configuration_idle());
    gWpaSm.wpa_sm_eap_disable=esp_wifi_sta_enterprise_disable;
    assert(!esp32qjs_eap_configuration_idle());gWpaSm.wpa_sm_eap_disable=NULL;
    g_wpa_password=(void *)1;assert(!esp32qjs_eap_configuration_idle());g_wpa_password=NULL;on_wifi=false;
    /* No mutation/driver call when outer dispatch or API lock fails. */
    fail_dispatch=true;assert(esp_wifi_sta_enterprise_enable()==ESP_FAIL && !enables && !allocations);fail_dispatch=false;
    fail_alloc=true;assert(esp_wifi_sta_enterprise_enable()==ESP_ERR_NO_MEM && !enables && !live);
    clean();assert(live==1); /* Existing SDK API mutex remains allocated. */
    fail_lock=true;assert(esp_wifi_sta_enterprise_enable()==ESP_ERR_INVALID_STATE && !enables);fail_lock=false;
    /* Driver already changed when callback allocation fails. Cleanup still reachable. */
    fail_alloc=true;assert(esp_wifi_sta_enterprise_enable()==ESP_ERR_NO_MEM && driver_enabled && gWpaSm.wpa_sm_eap_disable);
    unsigned n=enables;assert(esp_wifi_sta_enterprise_enable()==ESP_ERR_INVALID_STATE && enables==n);clean();
    fail_methods=true;n=method_clears;assert(esp_wifi_sta_enterprise_enable()==ESP_FAIL && driver_enabled && !methods && method_clears==n+1);fail_methods=false;clean();
    assert(esp_wifi_sta_enterprise_enable()==ESP_OK && driver_enabled && driver_callbacks && methods);
    n=enables;assert(esp_wifi_sta_enterprise_enable()==ESP_OK && enables==n);
    /* Confirmed disable write precedes failed worker retirement; retry doesn't write again. */
    gEapSm=(void *)1;g_wpa_password=(void *)2;fail_retire=true;n=global_clears;
    assert(esp_wifi_sta_enterprise_disable()==77 && !driver_enabled && gEapSm && g_wpa_password && global_clears==n);
    unsigned before=disables;fail_retire=false;clean();assert(disables==before);
    /* Unregister failure keeps methods/globals; only that unfinished suffix repeats. */
    assert(esp_wifi_sta_enterprise_enable()==0);g_wpa_password=(void *)2;unregister_error=88;
    assert(esp_wifi_sta_enterprise_disable()==88 && driver_callbacks && g_wpa_password);
    before=disables;unregister_error=0;clean();assert(disables==before);
    /* Unknown transport state blocks enable until an explicit clear succeeds. */
    driver_error=91;assert(esp_wifi_sta_enterprise_enable()==91 && esp32qjs_eap_driver_state<0);
    driver_error=0;n=enables;assert(esp_wifi_sta_enterprise_enable()==ESP_ERR_INVALID_STATE && enables==n);clean();
    /* Native task uses direct dispatch; worker task must not wait on Wi-Fi. */
    on_wifi=true;before=dispatches;assert(esp_wifi_sta_enterprise_enable()==0 && dispatches==before);assert(esp_wifi_sta_enterprise_disable()==0 && dispatches==before);on_wifi=false;
    s_wpa2_task_hdl=&eap_task_marker;on_eap=true;assert(esp32qjs_eap_on_worker());before=dispatches;assert(esp_wifi_sta_enterprise_enable()==ESP_ERR_INVALID_STATE && esp_wifi_sta_enterprise_disable()==ESP_ERR_INVALID_STATE && dispatches==before);on_eap=false;assert(!esp32qjs_eap_on_worker());s_wpa2_task_hdl=NULL;
    /* deattach stops before unrelated SAE/WPS/WPA cleanup when EAP clear fails. */
    assert(esp_wifi_sta_enterprise_enable()==0);gWpaSm.wpa_sm_wps_disable=clear_wps;gEapSm=(void *)1;fail_retire=true;
    on_wifi=true;assert(!wpa_deattach() && !sae_frees && !wps_frees && !wpa_frees && !roam_frees);on_wifi=false;
    fail_retire=false;on_wifi=true;assert(wpa_deattach() && sae_frees==1 && wps_frees==1 && wpa_frees==1 && roam_frees==1);on_wifi=false;
    /* Unexpected callback register result has ambiguous adoption. One bounded
     * quarantine, no second allocation, no repeated unregister after success. */
    for(unsigned adopt=0;adopt<2;adopt++) {
        register_error=93;adopt_on_error=adopt;assert(esp_wifi_sta_enterprise_enable()==93);
        assert(esp32qjs_eap_callback_quarantine);n=allocations;
        assert(esp_wifi_sta_enterprise_enable()==ESP_ERR_INVALID_STATE && allocations==n);
        assert(esp_wifi_sta_enterprise_disable()==ESP_ERR_INVALID_STATE && !driver_callbacks);
        before=unregisters;assert(esp_wifi_sta_enterprise_disable()==ESP_ERR_INVALID_STATE && unregisters==before);
        assert(live==1+!adopt);
        /* Fixture-only process-reset cleanup: the injected driver knows actual
         * adoption. Production deliberately cannot free this ambiguous pointer. */
        if(!adopt)os_free(esp32qjs_eap_callback_quarantine);
        esp32qjs_eap_callback_quarantine=NULL;register_error=0;clean();
    }
    assert(esp32qjs_eap_native_control_error()==0 && live==1 && !api_locked);
    os_free(s_wpa2_api_lock);s_wpa2_api_lock=NULL;assert(!live);return 0;
}
'''
