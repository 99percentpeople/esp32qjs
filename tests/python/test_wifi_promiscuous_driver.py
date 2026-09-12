"""Deferred production driver transaction + actual broker/parser/filter tests."""
import unittest
import json
import test_wifi_rx_target as rx_target
import test_wifi_promiscuous_broker as broker_fixture
from test_wireless_control_regression import compile_run


class WiFiPromiscuousDriver(unittest.TestCase):
    def test_driver_snapshot_readback_rollback_suffix_and_stable_callback(self):
        for profile in ['esp32c3/representative', 'esp32s3/representative-psram', 'esp32c5/representative']:
            with self.subTest(profile=profile):
                code = rx_target.WiFiRxTarget().production_code(profile) + broker_fixture.THREADS
                for name in ['wifi_rx_filter', 'wifi_promiscuous_broker']:
                    code += rx_target.unit(rx_target.INTERNAL / ('esp32_mquickjs_' + name + '.h'))
                    code += rx_target.unit(rx_target.COMMON / ('esp32_mquickjs_' + name + '.c'))
                symbols = json.loads((rx_target.ROOT / "docs/idf-wifi-api-inventory.json").read_text())["variants"][profile]["symbols"]
                code += "\n".join("#define " + v["declaration"] for k, v in symbols.items()
                                  if k.split("::")[-1].startswith(("WIFI_PROMIS_FILTER_MASK_", "WIFI_PROMIS_CTRL_FILTER_MASK_")) and v["kind"] == "macro") + "\n"
                code += SDK
                code += rx_target.unit(rx_target.INTERNAL / 'esp32_mquickjs_wifi_promiscuous_driver.h')
                code += rx_target.unit(rx_target.COMMON / 'esp32_mquickjs_wifi_promiscuous_driver.c')
                compile_run(self, code + MAIN)


SDK = r'''
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
typedef struct { uint32_t filter_mask; } wifi_promiscuous_filter_t;
typedef void (*wifi_promiscuous_cb_t)(void *,wifi_promiscuous_pkt_type_t);
static bool native_enabled;
static uint32_t native_packet=17,native_control=0x0d800000;
static wifi_promiscuous_cb_t native_callback;
static unsigned calls,failure1,failure2,packet_sets,control_sets,enable_sets,callback_sets;
static int boundary(void) {
    assert(!critical_depth);++calls;
    return calls==failure1 || calls==failure2 ? -(100+(int)calls) : ESP_OK;
}
/* Setters deliberately mutate even when returning an error. */
int esp_wifi_set_promiscuous(bool value) { int e=boundary();++enable_sets;native_enabled=value;return e; }
int esp_wifi_get_promiscuous(bool *value) { int e=boundary();if(!e)*value=native_enabled;return e; }
int esp_wifi_set_promiscuous_filter(const wifi_promiscuous_filter_t *value) { int e=boundary();++packet_sets;native_packet=value->filter_mask;return e; }
int esp_wifi_get_promiscuous_filter(wifi_promiscuous_filter_t *value) { int e=boundary();if(!e)value->filter_mask=native_packet;return e; }
int esp_wifi_set_promiscuous_ctrl_filter(const wifi_promiscuous_filter_t *value) { int e=boundary();++control_sets;native_control=value->filter_mask;return e; }
int esp_wifi_get_promiscuous_ctrl_filter(wifi_promiscuous_filter_t *value) { int e=boundary();if(!e)value->filter_mask=native_control;return e; }
int esp_wifi_set_promiscuous_rx_cb(wifi_promiscuous_cb_t value) { int e=boundary();++callback_sets;native_callback=value;return e; }
int64_t esp_timer_get_time(void) { return INT64_C(1)<<42; }
'''

MAIN = r'''
static unsigned received;
static void receive(void *context,const esp32_mquickjs_wifi_rx_target_view_t *view,
                    esp32_mquickjs_wifi_rx_filter_result_t result,uint64_t now) {
    assert(!critical_depth && context==&received && now==(UINT64_C(1)<<42));
    assert(result==ESP32_MQUICKJS_WIFI_RX_FILTER_ACCEPT && view->readable_length==24 && view->bytes[0]==8);
    ++received;
}
static void original(void) {
    esp32_mquickjs_wifi_promiscuous_driver_status_t status;
    esp32_mquickjs_wifi_promiscuous_driver_status(&status);
    assert(!status.claimed && !status.cleanup_pending && !native_enabled && !native_callback);
    assert(native_packet==17 && native_control==0x0d800000);
}
int main(void) {
    esp32_mquickjs_wifi_promiscuous_snapshot_t requirements={.active=1,.required_types=15,
        .required_control_subtypes=(1U<<11)|(1U<<13),.require_error_frames=true};
    esp32_mquickjs_wifi_promiscuous_demand_t converted;
    assert(esp32_mquickjs_wifi_promiscuous_driver_demand(&requirements,true,false,&converted)==ESP_OK);
    assert(converted.enabled && converted.receive && !converted.preserve_baseline && converted.packet_mask==0x4f);
    assert(converted.control_mask==(WIFI_PROMIS_CTRL_FILTER_MASK_RTS|WIFI_PROMIS_CTRL_FILTER_MASK_ACK));
    requirements.required_control_subtypes=1U<<5;
    assert(esp32_mquickjs_wifi_promiscuous_driver_demand(&requirements,true,true,&converted)==ESP_OK);
    assert(converted.preserve_baseline && converted.control_mask==WIFI_PROMIS_CTRL_FILTER_MASK_ALL);
    assert(esp32_mquickjs_wifi_promiscuous_driver_demand(&requirements,false,false,&converted)==ESP_ERR_INVALID_STATE);
    const esp32_mquickjs_wifi_promiscuous_demand_t raw={.enabled=true,.receive=true,.packet_mask=7,.control_mask=0xff800000};
    const esp32_mquickjs_wifi_promiscuous_demand_t off={0},csi={.enabled=true};
    esp32_mquickjs_wifi_promiscuous_driver_status_t status;
    assert(esp32_mquickjs_wifi_promiscuous_driver_apply(NULL)==ESP_ERR_INVALID_ARG);
    esp32_mquickjs_wifi_promiscuous_demand_t invalid={.receive=true};
    assert(esp32_mquickjs_wifi_promiscuous_driver_apply(&invalid)==ESP_ERR_INVALID_ARG && !calls);
    native_enabled=true;assert(esp32_mquickjs_wifi_promiscuous_driver_apply(&raw)==ESP_ERR_INVALID_STATE);
    assert(native_enabled && !packet_sets && !control_sets && !callback_sets && !enable_sets);
    native_enabled=false;calls=0;
    assert(esp32_mquickjs_wifi_promiscuous_driver_apply(&raw)==ESP_OK);
    unsigned forward_calls=calls;assert(forward_calls==10 && native_enabled && native_callback);
    assert(esp32_mquickjs_wifi_promiscuous_driver_apply(&off)==ESP_OK);original();
    /* An enable-only owner's saved requirements are unioned with RX demand,
     * then removed when that owner closes; exact baseline returns at the end. */
    esp32_mquickjs_wifi_promiscuous_demand_t shared={.enabled=true,.receive=true,.preserve_baseline=true,
        .packet_mask=2,.control_mask=0x20000000};
    assert(esp32_mquickjs_wifi_promiscuous_driver_apply(&shared)==ESP_OK);
    assert(native_packet==(17U|2U) && native_control==(0x0d800000U|0x20000000U));
    shared.preserve_baseline=false;
    assert(esp32_mquickjs_wifi_promiscuous_driver_apply(&shared)==ESP_OK);
    assert(native_packet==2 && native_control==0x20000000);
    assert(esp32_mquickjs_wifi_promiscuous_driver_apply(&off)==ESP_OK);original();
    for(unsigned failure=1;failure<=forward_calls;failure++) {
        calls=0;failure1=failure;
        assert(esp32_mquickjs_wifi_promiscuous_driver_apply(&raw)==-(100+(int)failure));
        esp32_mquickjs_wifi_promiscuous_driver_status(&status);
        assert(status.error==-(100+(int)failure) && status.error_stage && !status.cleanup_pending);
        original();failure1=0;
    }
    /* Forward packet readback fails; packet rollback succeeds, control rollback
     * fails. Retry only the uncompleted control suffix, preserving first error. */
    calls=0;failure1=7;failure2=10;packet_sets=control_sets=0;
    assert(esp32_mquickjs_wifi_promiscuous_driver_apply(&raw)==-107);
    esp32_mquickjs_wifi_promiscuous_driver_status(&status);
    assert(status.claimed && status.cleanup_pending && status.error==-107 && status.cleanup_error==-110);
    assert(!strcmp(status.cleanup_stage,"promiscuous-restore-control"));
    unsigned before=calls,packets=packet_sets;
    assert(esp32_mquickjs_wifi_promiscuous_driver_apply(&csi)==ESP_ERR_INVALID_STATE && calls==before);
    failure1=failure2=0;
    assert(esp32_mquickjs_wifi_promiscuous_driver_recover()==ESP_OK && packet_sets==packets && calls==before+2);
    original();
    assert(esp32_mquickjs_wifi_promiscuous_driver_apply(&raw)==ESP_OK);
    esp32_mquickjs_wifi_promiscuous_demand_t changed=raw;changed.control_mask=0x20000000;changed.packet_mask=5;
    calls=0;failure1=2;
    assert(esp32_mquickjs_wifi_promiscuous_driver_apply(&changed)==-102);
    assert(native_enabled && native_callback && native_control==raw.control_mask && native_packet==raw.packet_mask);
    esp32_mquickjs_wifi_promiscuous_driver_status(&status);assert(status.claimed && !status.cleanup_pending);
    failure1=0;

    /* Real callback -> boot registry -> native adapter/parser/filter -> sink. */
    esp32_mquickjs_wifi_rx_filter_t filter={.type_mask=4,.sample_every=1,.valid_only=true};
    esp32_mquickjs_wifi_promiscuous_subscriber_t subscriber;
    esp32_mquickjs_wifi_promiscuous_token_t token={0};
    assert(esp32_mquickjs_wifi_promiscuous_reserve(&subscriber,&filter,receive,&received,&token)==ESP32_MQUICKJS_WIFI_PROMISCUOUS_OK);
    assert(esp32_mquickjs_wifi_promiscuous_activate(&token)==ESP32_MQUICKJS_WIFI_PROMISCUOUS_OK);
    wifi_pkt_rx_ctrl_t rx;memset(&rx,0,sizeof(rx));rx.sig_len=24;
#if CONFIG_SOC_WIFI_HE_SUPPORT
    rx.dump_len=24;
#endif
    uint8_t packet[sizeof(rx)+24];memset(packet,0,sizeof(packet));memcpy(packet,&rx,sizeof(rx));packet[sizeof(rx)]=8;
    wifi_promiscuous_cb_t late=native_callback;native_callback(packet,WIFI_PKT_DATA);assert(received==1);
    assert(esp32_mquickjs_wifi_promiscuous_begin_close(&token)==ESP32_MQUICKJS_WIFI_PROMISCUOUS_OK);
    assert(esp32_mquickjs_wifi_promiscuous_driver_apply(&csi)==ESP_OK);
    assert(native_enabled && !native_callback && native_packet==17 && native_control==0x0d800000);
    assert(esp32_mquickjs_wifi_promiscuous_finish_close(&token)==ESP32_MQUICKJS_WIFI_PROMISCUOUS_OK);
    late(packet,WIFI_PKT_DATA);assert(received==1);
    unsigned no_change=calls;assert(esp32_mquickjs_wifi_promiscuous_driver_apply(&csi)==ESP_OK && calls==no_change);
    assert(esp32_mquickjs_wifi_promiscuous_driver_apply(&off)==ESP_OK);original();
}
'''
