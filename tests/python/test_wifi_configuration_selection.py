"""Production selection/admission and semantic Station readback; phase run deferred.

SDK types are recorded declarations. The real Radio mutex boundary is injected,
with a mutation on entry to prove defaults are read after acquisition. No SDK
function is supplied: resolution/admission must not call any driver function.
"""
import re
import unittest
from wireless_vm_fixture import ROOT, extract
from test_wifi_config_controls import PRELUDE, sdk_types, structure
from test_wireless_control_regression import compile_run

HEADER = ROOT / 'components/esp32_mquickjs/internal/esp32_mquickjs_wifi_radio.h'
RADIO = ROOT / 'components/esp32_mquickjs/src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c'

BOUNDARIES = r'''
#define ESP32_MQUICKJS_WIFI_RADIO_UNINITIALIZED 0
#define ESP32_MQUICKJS_WIFI_RADIO_STARTED 4
static struct {
    int lock,driver_state;bool storage_configured,started,restart_required,stop_required,promiscuous_claimed;
    const char *fault_stage,*cleanup_stage;
    wifi_mode_t effective_mode;wifi_storage_t storage;
    unsigned generation,next_lifecycle_identity,wake_locks;
    esp32_mquickjs_wifi_radio_lifecycle_t lifecycle;
    struct {unsigned identity;} operation;
    struct {unsigned identity;esp32_mquickjs_wifi_radio_client_t client;} leases[WIFI_RADIO_MAX_LEASES];
} s_radio;
static int locked,critical;static bool change_at_lock;
static void wifi_radio_operation_lock(void) {
    assert(!locked && !critical);locked=1;
    if(change_at_lock) {
        s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTED;s_radio.storage_configured=true;
        s_radio.started=true;s_radio.stop_required=true;s_radio.effective_mode=WIFI_MODE_AP;s_radio.storage=WIFI_STORAGE_FLASH;
    }
}
static void wifi_radio_operation_unlock(void) {assert(locked && !critical);locked=0;}
#define taskENTER_CRITICAL(p) do {(void)(p);assert(locked && !critical);critical=1;} while(0)
#define taskEXIT_CRITICAL(p) do {(void)(p);assert(locked && critical);critical=0;} while(0)
static void reset_fixture(void) {
    assert(!locked && !critical);memset(&s_radio,0,sizeof(s_radio));
    s_radio.generation=7;s_radio.next_lifecycle_identity=11;change_at_lock=false;
}
'''

MAIN = r'''
static int begin(esp32_mquickjs_wifi_radio_configuration_selection_t *selection,
                 const esp32_mquickjs_wifi_radio_lease_t *sta,
                 const esp32_mquickjs_wifi_radio_lease_t *ap,
                 esp32_mquickjs_wifi_radio_lifecycle_t *token) {
    return esp32_mquickjs_wifi_radio_begin_configuration_lifecycle(NULL,sta,ap,selection,NULL,NULL,token);
}
static void reject(esp32_mquickjs_wifi_radio_configuration_selection_t request,int expected) {
    esp32_mquickjs_wifi_radio_configuration_selection_t before=request;
    esp32_mquickjs_wifi_radio_lifecycle_t token={0};unsigned identity=s_radio.next_lifecycle_identity;
    assert(begin(&request,NULL,NULL,&token)==expected);
    assert(!memcmp(&request,&before,sizeof(request)) && !token.identity && !locked && !critical);
    assert(s_radio.next_lifecycle_identity==identity);
}
int main(void) {
    for(unsigned interfaces=0;interfaces<4;interfaces++) {
        reset_fixture();esp32_mquickjs_wifi_radio_lifecycle_t token={0};
        esp32_mquickjs_wifi_radio_configuration_selection_t request={.station_set=!!(interfaces&1),.access_point_set=!!(interfaces&2)};
#if !CONFIG_ESP_WIFI_SOFTAP_SUPPORT
        if(interfaces&2) {reject(request,ESP_ERR_NOT_SUPPORTED);continue;}
#endif
        assert(begin(&request,NULL,NULL,&token)==ESP_OK);
        assert(request.mode==(interfaces?interfaces:WIFI_MODE_STA) && request.storage==WIFI_STORAGE_RAM && request.start);
        assert(!request.mode_set && !request.storage_set && !request.start_set);
        assert(token.identity==11 && token.generation==7 && s_radio.lifecycle.identity==11);
        esp32_mquickjs_wifi_radio_lifecycle_t other={0};
        assert(begin(&request,NULL,NULL,&other)==ESP_ERR_INVALID_STATE && !other.identity);
    }
    reset_fixture();esp32_mquickjs_wifi_radio_configuration_selection_t request={0};
    request.mode_set=true;request.mode=WIFI_MODE_NULL;reject(request,ESP_ERR_INVALID_ARG);
    request=(esp32_mquickjs_wifi_radio_configuration_selection_t){.storage_set=true,.storage=9};reject(request,ESP_ERR_INVALID_ARG);
    request=(esp32_mquickjs_wifi_radio_configuration_selection_t){.mode_set=true,.mode=WIFI_MODE_STA,.access_point_set=true};reject(request,ESP_ERR_INVALID_ARG);
    request=(esp32_mquickjs_wifi_radio_configuration_selection_t){.mode_set=true,.mode=WIFI_MODE_AP,.start_set=true,.start=true};reject(request,ESP_ERR_INVALID_ARG);
    request=(esp32_mquickjs_wifi_radio_configuration_selection_t){0};
    s_radio.fault_stage="init";reject(request,ESP_ERR_INVALID_STATE);s_radio.fault_stage=NULL;
    s_radio.restart_required=true;reject(request,ESP_ERR_INVALID_STATE);s_radio.restart_required=false;
    s_radio.stop_required=true;reject(request,ESP_ERR_INVALID_STATE);s_radio.stop_required=false;
    s_radio.cleanup_stage="stop";reject(request,ESP_ERR_INVALID_STATE);s_radio.cleanup_stage=NULL;
    s_radio.driver_state=3;reject(request,ESP_ERR_INVALID_STATE);s_radio.driver_state=0;
    s_radio.wake_locks=1;reject(request,ESP_ERR_INVALID_STATE);s_radio.wake_locks=0;
    s_radio.operation.identity=1;reject(request,ESP_ERR_INVALID_STATE);s_radio.operation.identity=0;
    s_radio.promiscuous_claimed=true;reject(request,ESP_ERR_INVALID_STATE);s_radio.promiscuous_claimed=false;
    s_radio.leases[0].identity=1;s_radio.leases[0].client=ESP32_MQUICKJS_WIFI_RADIO_CLIENT_ESPNOW;
    reject(request,ESP_ERR_INVALID_STATE);s_radio.leases[0].identity=0;
    s_radio.next_lifecycle_identity=0;reject(request,ESP_ERR_NO_MEM);

    reset_fixture();s_radio.storage_configured=true;s_radio.started=true;s_radio.stop_required=true;
    s_radio.storage=WIFI_STORAGE_RAM;s_radio.effective_mode=WIFI_MODE_STA;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTED;
    esp32_mquickjs_wifi_radio_lifecycle_t running_token={0};
    assert(begin(&request,NULL,NULL,&running_token)==ESP_OK && request.start && request.mode==WIFI_MODE_STA);
    request=(esp32_mquickjs_wifi_radio_configuration_selection_t){0};
    reset_fixture();s_radio.storage_configured=true;s_radio.storage=WIFI_STORAGE_FLASH;
    s_radio.effective_mode=WIFI_MODE_STA;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STOPPED;
    esp32_mquickjs_wifi_radio_lifecycle_t token={0};
    esp32_mquickjs_wifi_radio_start_controls_t power={.tx_power_set=true,.tx_power_quarter_dbm=72};
    assert(esp32_mquickjs_wifi_radio_begin_configuration_lifecycle(NULL,NULL,NULL,&request,NULL,&power,&token)==ESP_ERR_INVALID_ARG);
    assert(!token.identity && !request.start_set && !request.storage_set);
    assert(begin(&request,NULL,NULL,&token)==ESP_OK && !request.start && request.storage==WIFI_STORAGE_FLASH && request.mode==WIFI_MODE_STA);
#if CONFIG_ESP_WIFI_SOFTAP_SUPPORT
    reset_fixture();request=(esp32_mquickjs_wifi_radio_configuration_selection_t){.access_point_set=true};
    change_at_lock=true;reject(request,ESP_ERR_INVALID_STATE); /* Running AP, even without a client snapshot. */
    request.allow_disconnect=true;token=(esp32_mquickjs_wifi_radio_lifecycle_t){0};
    assert(begin(&request,NULL,NULL,&token)==ESP_OK && request.mode==WIFI_MODE_AP && request.start && request.storage==WIFI_STORAGE_FLASH);
    reset_fixture();s_radio.storage_configured=true;s_radio.effective_mode=WIFI_MODE_AP;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STOPPED;
    request=(esp32_mquickjs_wifi_radio_configuration_selection_t){0};token=(esp32_mquickjs_wifi_radio_lifecycle_t){0};
    assert(begin(&request,NULL,NULL,&token)==ESP_OK && !request.start && request.mode==WIFI_MODE_AP);
    reset_fixture();request=(esp32_mquickjs_wifi_radio_configuration_selection_t){.station_set=true,.access_point_set=true,.allow_disconnect=true};
    esp32_mquickjs_wifi_radio_lease_t sta={7,5,ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_STA,true};
    esp32_mquickjs_wifi_radio_lease_t ap={7,6,ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_AP,true};
    s_radio.leases[0].identity=5;s_radio.leases[0].client=sta.client;s_radio.leases[1].identity=6;s_radio.leases[1].client=ap.client;
    token=(esp32_mquickjs_wifi_radio_lifecycle_t){0};
    assert(begin(&request,&ap,&sta,&token)==ESP_ERR_INVALID_STATE && !token.identity);
    sta.generation=6;assert(begin(&request,&sta,&ap,&token)==ESP_ERR_INVALID_STATE && !token.identity);sta.generation=7;
    assert(begin(&request,&sta,&ap,&token)==ESP_OK && sta.acquired && ap.acquired);
#endif
    /* Direct disabled-PMF connect supplies only STA, preserves defaults and
     * requests START. Even explicit Station disconnect permission must not
     * retire an existing AP or another feature's lease. */
    for(int mode=WIFI_MODE_AP;mode<=WIFI_MODE_APSTA;mode++) {
        reset_fixture();s_radio.storage_configured=true;s_radio.effective_mode=mode;
        s_radio.storage=WIFI_STORAGE_FLASH;s_radio.started=true;
        s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTED;
        request=(esp32_mquickjs_wifi_radio_configuration_selection_t){
            .station_set=true,.start_set=true,.start=true,.allow_disconnect=true};
        reject(request,ESP_ERR_INVALID_ARG);
    }
    reset_fixture();s_radio.leases[0].identity=44;
    s_radio.leases[0].client=ESP32_MQUICKJS_WIFI_RADIO_CLIENT_ESPNOW;
    request=(esp32_mquickjs_wifi_radio_configuration_selection_t){
        .station_set=true,.start_set=true,.start=true,.allow_disconnect=true};
    reject(request,ESP_ERR_INVALID_STATE);
    /* Explicit AP activation resolves current mode/storage in the same lock
     * as admission. Ordinary configure must keep its original default rules. */
    for(int previous=WIFI_MODE_NULL;previous<=WIFI_MODE_APSTA;previous++) {
        reset_fixture();
        s_radio.storage_configured=previous!=WIFI_MODE_NULL;
        s_radio.effective_mode=previous;s_radio.storage=WIFI_STORAGE_FLASH;
        s_radio.driver_state=previous?ESP32_MQUICKJS_WIFI_RADIO_STARTED:ESP32_MQUICKJS_WIFI_RADIO_UNINITIALIZED;
        s_radio.started=s_radio.stop_required=previous!=WIFI_MODE_NULL;
        request=(esp32_mquickjs_wifi_radio_configuration_selection_t){
            .activate_ap=true,.access_point_set=true,.start_set=true,.start=true,.allow_disconnect=true};
#if !CONFIG_ESP_WIFI_SOFTAP_SUPPORT
        reject(request,ESP_ERR_NOT_SUPPORTED);
#else
        token=(esp32_mquickjs_wifi_radio_lifecycle_t){0};
        assert(begin(&request,NULL,NULL,&token)==ESP_OK);
        assert(request.mode==(previous&WIFI_MODE_STA?WIFI_MODE_APSTA:WIFI_MODE_AP));
        assert(request.storage==(previous?WIFI_STORAGE_FLASH:WIFI_STORAGE_RAM));
        assert(request.start && !request.station_set && !request.mode_set && !request.storage_set);
#endif
    }
    reset_fixture();
    request=(esp32_mquickjs_wifi_radio_configuration_selection_t){
        .activate_ap=true,.access_point_set=true,.start_set=true,.start=true,.allow_disconnect=true};
    for(int invalid=0;invalid<7;invalid++) {
        esp32_mquickjs_wifi_radio_configuration_selection_t bad=request;
        if(invalid==0)bad.allow_disconnect=false;
        if(invalid==1)bad.station_set=true;
        if(invalid==2)bad.mode_set=true;
        if(invalid==3)bad.storage_set=true;
        if(invalid==4)bad.start_set=false;
        if(invalid==5)bad.start=false;
        if(invalid==6)bad.start_only=true;
        reject(bad,ESP_ERR_INVALID_ARG);
    }
#if CONFIG_ESP_WIFI_SOFTAP_SUPPORT
    change_at_lock=true;token=(esp32_mquickjs_wifi_radio_lifecycle_t){0};
    assert(begin(&request,NULL,NULL,&token)==ESP_OK);
    assert(request.mode==WIFI_MODE_AP && request.storage==WIFI_STORAGE_FLASH);
    reset_fixture();request.mode=WIFI_MODE_NULL;
    s_radio.leases[0].identity=45;s_radio.leases[0].client=ESP32_MQUICKJS_WIFI_RADIO_CLIENT_ESPNOW;
    reject(request,ESP_ERR_INVALID_STATE);
    reset_fixture();s_radio.operation.identity=71;reject(request,ESP_ERR_INVALID_STATE);
#endif
    wifi_config_t a={0},b={0};memcpy(a.sta.ssid,"station",7);memcpy(a.sta.password,"credential",10);
    a.sta.pmf_cfg.required=true;a.sta.sae_pk_mode=WPA3_SAE_PK_MODE_ONLY;b=a;
    assert(esp32_mquickjs_wifi_radio_accept_station_config(&a,&b));
    b.sta.password[0]^=1;assert(!esp32_mquickjs_wifi_radio_accept_station_config(&a,&b));b=a;
    b.sta.ssid[1]^=1;assert(!esp32_mquickjs_wifi_radio_accept_station_config(&a,&b));b=a;
    b.sta.pmf_cfg.required=false;assert(!esp32_mquickjs_wifi_radio_accept_station_config(&a,&b));b=a;
    b.sta.sae_pk_mode=WPA3_SAE_PK_MODE_DISABLED;assert(!esp32_mquickjs_wifi_radio_accept_station_config(&a,&b));
    assert(!esp32_mquickjs_wifi_radio_accept_station_config(NULL,&b));
    return 0;
}
'''

class WiFiConfigurationSelection(unittest.TestCase):
    def test_defaults_authorization_exact_owners_and_readback(self):
        header, radio = HEADER.read_text(), RADIO.read_text()
        enums = '\n'.join(re.search(r'typedef enum \{[^}]*\} ' + name + r';', header).group(0)
                          for name in ('esp32_mquickjs_wifi_radio_client_t',))
        structs = '\n'.join(structure(header, name) for name in (
            'esp32_mquickjs_wifi_radio_lease_t', 'esp32_mquickjs_wifi_radio_lifecycle_t',
            'esp32_mquickjs_wifi_radio_config_controls_t', 'esp32_mquickjs_wifi_radio_start_controls_t',
            'esp32_mquickjs_wifi_radio_configuration_selection_t'))
        functions = ''.join(extract(radio, name) for name in (
            'wifi_radio_lease_valid', 'wifi_radio_begin_lifecycle_with_dependents_locked', 'wifi_radio_begin_lifecycle_locked',
            'esp32_mquickjs_wifi_radio_5ghz_channel_bit', 'wifi_radio_validate_protocol',
            'esp32_mquickjs_wifi_radio_validate_config_controls', 'esp32_mquickjs_wifi_radio_validate_start_controls',
            'esp32_mquickjs_wifi_radio_begin_configuration_lifecycle', 'wifi_radio_config_equal',
            'esp32_mquickjs_wifi_radio_accept_station_config'))
        for profile, ap, five, he in [('esp32c5',1,1,1),('esp32c3',1,0,0),('esp32c3',0,0,0)]:
            with self.subTest(target=profile, softap=ap):
                defines = (f'#define CONFIG_ESP_WIFI_SOFTAP_SUPPORT {ap}\n#define CONFIG_SOC_WIFI_SUPPORT_5G {five}\n'
                           f'#define CONFIG_SOC_WIFI_HE_SUPPORT {he}\n')
                compile_run(self, PRELUDE + defines + sdk_types(profile + '/representative') + enums + structs + BOUNDARIES + functions + MAIN)
