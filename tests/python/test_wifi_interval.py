"""Deferred production connectionless interval ledger; no Radio/ESP-NOW integration claim."""
import unittest
from test_wifi_rx_target import unit
from test_wifi_driver_phy import COMPONENT
from test_wireless_control_regression import compile_run


class WiFiInterval(unittest.TestCase):
    def test_real_writer_unknown_baseline_exact_owner_restore_suffix_and_exhaustion(self):
        code = PRELUDE + unit(COMPONENT / 'internal/esp32_mquickjs_wifi_interval.h')
        code += unit(COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_interval.c')
        compile_run(self, code + MAIN)

    def test_real_frozen_capture_replay_rebuild_and_revision_capacity(self):
        code = PRELUDE + unit(COMPONENT / 'internal/esp32_mquickjs_wifi_interval.h')
        code += unit(COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_interval.c')
        compile_run(self, code + REPLAY_MAIN)


PRELUDE = r'''
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>
#define CONFIG_ESP32_MQUICKJS_WIFI_RADIO 1
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_STATE 2
'''

MAIN = r'''
static esp32_mquickjs_wifi_interval_state_t state;
static esp32_mquickjs_wifi_interval_result_t result;
static esp32_mquickjs_wifi_interval_token_t token;
static uint16_t native_interval;
static unsigned calls;
static int failure;
static bool expect_owner;
static int writer(void *opaque,uint16_t value) {
    assert(opaque==&state);++calls;native_interval=value;
    assert((state.owner.identity!=0)==expect_owner);
    if(expect_owner)assert(state.restore_pending && state.owner.identity==token.identity);
    return failure; /* Model SDK side effect before an error is reported. */
}
#define WRITE(gen,value) esp32_mquickjs_wifi_interval_write(&state,gen,value,writer,&state,&result)
#define ACQUIRE(gen,id,value) esp32_mquickjs_wifi_interval_acquire(&state,gen,id,value,writer,&state,&token,&result)
#define UPDATE(tok,value) esp32_mquickjs_wifi_interval_update(&state,tok,value,writer,&state,&result)
#define RELEASE(tok) esp32_mquickjs_wifi_interval_release(&state,tok,writer,&state,&result)
int main(void) {
    assert(ACQUIRE(7,12,100)==ESP_ERR_INVALID_STATE && !calls && !token.identity); /* Never guess baseline. */
    failure=77;assert(WRITE(7,0)==77 && result.attempted && !state.known && state.uncertain && native_interval==0);
    assert(ACQUIRE(7,12,100)==ESP_ERR_INVALID_STATE && calls==1);
    failure=0;assert(WRITE(7,300)==ESP_OK && state.known && state.value==300 && state.revision==2);
    unsigned before=calls;assert(WRITE(8,0)==ESP_ERR_INVALID_STATE && calls==before);
    expect_owner=true;assert(ACQUIRE(7,12,100)==ESP_OK && token.identity==3 && state.previous==300);
    esp32_mquickjs_wifi_interval_token_t old=token,bad=token;bad.owner_identity++;
    assert(UPDATE(&bad,200)==ESP_ERR_INVALID_STATE && calls==before+1);
    bad=token;bad.generation++;assert(RELEASE(&bad)==ESP_ERR_INVALID_STATE);
    assert(WRITE(7,0)==ESP_ERR_INVALID_STATE);assert(!esp32_mquickjs_wifi_interval_invalidate(&state,7));
    assert(ACQUIRE(7,12,200)==ESP_ERR_INVALID_STATE && token.identity==old.identity); /* Do not erase a live token. */
    assert(UPDATE(&token,200)==ESP_OK && state.previous==300 && native_interval==200);
    failure=77;assert(UPDATE(&token,400)==77 && state.uncertain && !state.known && state.restore_pending && token.identity);
    before=calls;failure=0;assert(UPDATE(&token,500)==ESP_ERR_INVALID_STATE && calls==before);
    failure=88;assert(RELEASE(&token)==88 && token.identity && state.previous==300 && state.restore_pending);
    assert(state.error==77 && state.restore_error==88 && native_interval==300);
    failure=0;assert(RELEASE(&token)==ESP_OK && !token.identity && !state.owner.identity && state.known && !state.uncertain && state.value==300);
    assert(state.error==77 && state.restore_error==ESP_OK);
    before=calls;assert(RELEASE(&token)==ESP_OK && calls==before);
    assert(ACQUIRE(7,12,100)==ESP_OK && token.identity!=old.identity); /* Same Radio lease, new interval identity. */
    before=calls;assert(RELEASE(&old)==ESP_ERR_INVALID_STATE && calls==before && state.owner.identity==token.identity);
    esp32_mquickjs_wifi_interval_token_t consumed={0};assert(RELEASE(&consumed)==ESP_OK && calls==before && state.owner.identity==token.identity);
    assert(RELEASE(&token)==ESP_OK);
    unsigned revision=state.revision;assert(esp32_mquickjs_wifi_interval_invalidate(&state,7) && !state.known && !state.generation && state.revision==revision);
    assert(ACQUIRE(8,12,100)==ESP_ERR_INVALID_STATE);
    expect_owner=false;assert(WRITE(8,0)==ESP_OK && state.value==0 && state.known);
    expect_owner=true;failure=77;assert(ACQUIRE(8,13,65535)==77 && token.identity && state.restore_pending && state.previous==0);
    failure=0;assert(RELEASE(&token)==ESP_OK && state.value==0);
    state.revision=UINT32_MAX-2;
    assert(ACQUIRE(8,13,100)==ESP_OK && token.identity==UINT32_MAX-1);
    before=calls;assert(UPDATE(&token,200)==ESP_ERR_INVALID_STATE && calls==before);
    assert(RELEASE(&token)==ESP_OK && state.revision==UINT32_MAX);
    assert(esp32_mquickjs_wifi_interval_invalidate(&state,8) && state.revision==UINT32_MAX);
    expect_owner=false;before=calls;assert(WRITE(9,0)==ESP_ERR_INVALID_STATE && calls==before && !result.attempted);
    memset(&state,0,sizeof(state));failure=0;assert(WRITE(10,100)==ESP_OK);
    state.revision=UINT32_MAX-2;expect_owner=true;assert(ACQUIRE(10,14,200)==ESP_OK);
    failure=88;assert(RELEASE(&token)==88 && token.identity && state.revision==UINT32_MAX);
    failure=0;before=calls;assert(RELEASE(&token)==ESP_ERR_INVALID_STATE && token.identity && calls==before && state.restore_pending);
    return 0;
}
'''


REPLAY_MAIN = r'''
static esp32_mquickjs_wifi_interval_state_t state;
static esp32_mquickjs_wifi_interval_snapshot_t snapshot;
static esp32_mquickjs_wifi_interval_result_t result;
static unsigned calls;static int error;static uint16_t native_value;
static int writer(void *opaque,uint16_t value) {assert(opaque==&state);++calls;native_value=value;return error;}
#define WRITE(gen,value) esp32_mquickjs_wifi_interval_write(&state,gen,value,writer,&state,&result)
#define CAPTURE(gen) esp32_mquickjs_wifi_interval_capture(&state,gen,&snapshot)
#define REPLAY(gen) esp32_mquickjs_wifi_interval_replay(&state,gen,&snapshot,writer,&state,&result)
int main(void) {
    assert(CAPTURE(7)==ESP_ERR_INVALID_STATE && !snapshot.generation);
    assert(WRITE(7,0)==ESP_OK && CAPTURE(7)==ESP_OK && snapshot.value==0);
    unsigned before=calls;assert(REPLAY(7)==ESP_ERR_INVALID_STATE && !result.attempted && calls==before);
    assert(WRITE(7,300)==ESP_OK && CAPTURE(7)==ESP_OK && snapshot.value==300);
    esp32_mquickjs_wifi_interval_snapshot_t frozen=snapshot;
    esp32_mquickjs_wifi_interval_token_t token={0};
    assert(esp32_mquickjs_wifi_interval_acquire(&state,7,41,100,writer,&state,&token,&result)==ESP_OK);
    assert(CAPTURE(7)==ESP_ERR_INVALID_STATE && !snapshot.generation);
    assert(esp32_mquickjs_wifi_interval_release(&state,&token,writer,&state,&result)==ESP_OK);
    assert(CAPTURE(7)==ESP_OK);frozen=snapshot;
    assert(CAPTURE(8)==ESP_ERR_INVALID_STATE && !snapshot.generation);snapshot=frozen;
    assert(esp32_mquickjs_wifi_interval_invalidate(&state,7));
    before=calls;assert(REPLAY(8)==ESP_ERR_INVALID_STATE && calls==before); /* No accepted new baseline. */
    assert(WRITE(8,0)==ESP_OK);error=77;
    assert(REPLAY(8)==77 && result.attempted && state.uncertain && !state.known);
    assert(frozen.generation==snapshot.generation && frozen.revision==snapshot.revision && frozen.value==snapshot.value && native_value==300);
    before=calls;error=0;assert(REPLAY(8)==ESP_ERR_INVALID_STATE && calls==before);
    assert(CAPTURE(8)==ESP_ERR_INVALID_STATE && !snapshot.generation);snapshot=frozen;
    assert(esp32_mquickjs_wifi_interval_invalidate(&state,8) && WRITE(9,0)==ESP_OK);
    assert(REPLAY(9)==ESP_OK && state.known && !state.uncertain && state.value==300);
    state.revision=UINT32_MAX-1;assert(CAPTURE(9)==ESP_ERR_INVALID_STATE && !snapshot.generation);
    state.revision=UINT32_MAX-2;assert(CAPTURE(9)==ESP_OK);
    assert(esp32_mquickjs_wifi_interval_invalidate(&state,9) && WRITE(10,0)==ESP_OK);
    assert(REPLAY(10)==ESP_OK && state.revision==UINT32_MAX && native_value==300);
    assert(CAPTURE(10)==ESP_ERR_INVALID_STATE && !snapshot.generation);
    return 0;
}
'''
