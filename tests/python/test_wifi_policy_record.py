"""Deferred production write-only policy ledger; not SDK/RF replay proof."""
import unittest
from test_wifi_rx_target import unit
from test_wifi_driver_phy import COMPONENT
from test_wireless_control_regression import compile_run


class WiFiPolicyRecord(unittest.TestCase):
    def test_real_record_acceptance_failure_history_invalidation_and_exhaustion(self):
        code = PRELUDE + unit(COMPONENT / 'internal/esp32_mquickjs_wifi_policy.h')
        code += unit(COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_policy.c')
        compile_run(self, code + MAIN)


PRELUDE = r'''
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>
#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_STATE 2
'''

MAIN = r'''
static esp32_mquickjs_wifi_policy_state_t state;
static esp32_mquickjs_wifi_policy_write_t result;
static unsigned calls;
static int failure;
static bool native[ESP32_MQUICKJS_WIFI_POLICY_SLOT_COUNT];
static int writer(void *opaque,esp32_mquickjs_wifi_policy_slot_t slot,bool requested) {
    assert(opaque==&state);++calls;native[slot]=requested;
    const esp32_mquickjs_wifi_policy_record_t *r=&state.records[slot];
    assert(result.attempted && !result.accepted && !r->known && r->uncertain);
    assert(r->revision==state.revision && result.revision==r->revision && r->requested==requested);
    return failure; /* Side effect can precede SDK error. */
}
#define APPLY(gen,slot,value) esp32_mquickjs_wifi_policy_apply(&state,gen,slot,value,writer,&state,&result)
int main(void) {
    assert(APPLY(0,0,true)==ESP_ERR_INVALID_ARG && !calls && !result.attempted);
    assert(APPLY(7,-1,true)==ESP_ERR_INVALID_ARG && !calls);
    assert(APPLY(7,ESP32_MQUICKJS_WIFI_POLICY_SLOT_COUNT,true)==ESP_ERR_INVALID_ARG && !calls);
    assert(APPLY(7,0,false)==ESP_OK && result.accepted && state.revision==1);
    assert(state.records[0].configured && state.records[0].known && !state.records[0].value);
    assert(APPLY(7,0,false)==ESP_OK && calls==2 && state.records[0].accepted_revision==2);
    assert(APPLY(8,1,true)==ESP_ERR_INVALID_STATE && calls==2 && !result.attempted);
    failure=77;
    assert(APPLY(7,0,true)==77 && native[0] && !result.accepted && result.attempted);
    assert(!state.records[0].known && state.records[0].uncertain && state.records[0].configured);
    assert(!state.records[0].value && state.records[0].requested && state.records[0].accepted_revision==2 && state.records[0].revision==3);
    assert(APPLY(7,1,true)==77 && !state.records[1].configured && !state.records[1].accepted_revision);
    assert(state.records[0].error==77 && state.records[1].error==77);
    failure=0;assert(APPLY(7,0,true)==ESP_OK && state.records[0].known && !state.records[0].uncertain && state.records[0].value);
    unsigned before=calls;uint32_t revision=state.revision;
    esp32_mquickjs_wifi_policy_invalidate(&state);
    assert(!state.records[0].known && !state.records[0].uncertain && state.records[0].configured && state.records[0].value);
    assert(!state.records[1].known && !state.records[1].uncertain && !state.records[1].configured && state.records[1].error==77);
    assert(state.revision==revision && calls==before);
    assert(APPLY(8,1,false)==ESP_OK && state.revision==revision+1 && state.records[1].generation==8);
    assert(!state.records[0].known && state.records[0].generation==7); /* History is not replay. */
    assert(APPLY(7,2,true)==ESP_ERR_INVALID_STATE && calls==before+1);
    state.revision=UINT32_MAX-1;
    assert(APPLY(8,2,false)==ESP_OK && state.revision==UINT32_MAX && state.records[2].configured);
    before=calls;esp32_mquickjs_wifi_policy_state_t saved=state;
    assert(APPLY(8,2,true)==ESP_ERR_INVALID_STATE && !result.attempted && calls==before && !memcmp(&state,&saved,sizeof(state)));
    esp32_mquickjs_wifi_policy_invalidate(&state);
    assert(APPLY(9,3,true)==ESP_ERR_INVALID_STATE && calls==before && state.revision==UINT32_MAX);
    return 0;
}
'''
