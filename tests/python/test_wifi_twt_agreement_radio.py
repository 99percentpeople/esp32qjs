"""Deferred actual multi-owner Radio setup/close paths; native calls are boundaries.

No fixture import, compilation or execution until the Wi-Fi validation stage.
Does not stand in for native SDK scheduling, RF teardown or full Future core.
"""
from pathlib import Path
import re
import unittest
from test_wifi_twt_radio import radio_code, INTERNAL, SOURCE
from test_wifi_config_controls import structure
from test_wifi_rx_target import unit
from test_wireless_control_regression import compile_run
from wireless_vm_fixture import extract


def agreement_radio_code():
    source = SOURCE.read_text()
    sdk = Path('/home/zach/esp/esp-idf/components/esp_wifi/include/esp_wifi_he_types.h').read_text()
    code = '#define TEST_TWT_AGREEMENT_RADIO 1\n' + radio_code()
    code += '\n#include <stdlib.h>\n#define ESP_ERR_NOT_FINISHED 0x104\n'
    code += re.search(r'typedef enum \{[^}]*\} wifi_twt_setup_cmds_t;', sdk).group(0)
    code += structure(sdk, 'wifi_twt_setup_config_t')
    code += '\ntypedef wifi_twt_setup_config_t wifi_itwt_setup_config_t;\n'
    code += structure(sdk, 'wifi_event_sta_itwt_setup_t')
    code += structure((INTERNAL / 'esp32_mquickjs_wifi_twt_options.h').read_text(), 'esp32_mquickjs_wifi_itwt_options_t')
    code += structure((INTERNAL / 'esp32_mquickjs_wifi_twt_sdk.h').read_text(), 'esp32_mquickjs_wifi_twt_setup_cut_t')
    code += structure(sdk, 'wifi_btwt_setup_config_t')
    code += re.search(r'typedef enum \{[^}]*\} wifi_btwt_setup_status_t;', sdk).group(0)
    code += structure(sdk, 'wifi_event_sta_btwt_setup_t')
    code += structure((INTERNAL / 'esp32_mquickjs_wifi_twt_options.h').read_text(), 'esp32_mquickjs_wifi_btwt_options_t')
    for name in ('teardown_tx', 'broadcast_timer', 'broadcast_submit', 'broadcast_retire', 'information_timer', 'information', 'setup_result', 'setup_submit', 'setup_retire', 'agreement_radio'):
        code += unit(INTERNAL / f'esp32_mquickjs_wifi_twt_{name}.h')
    code += structure(source, 'wifi_radio_twt_individual_t')
    code += '\nstatic wifi_radio_twt_individual_t *s_twt_individual;\n'
    code += '#define ESP32_MQUICKJS_WIFI_TWT_MAX_SLEEP_US (UINT64_C(1)<<35)\n'
    options = (SOURCE.parent.parent / 'wifi_twt/esp32_mquickjs_wifi_twt_options.c').read_text()
    for name in ('esp32_mquickjs_wifi_itwt_interval_us', 'esp32_mquickjs_wifi_itwt_duration_us', 'esp32_mquickjs_wifi_itwt_options_valid'):
        code += extract(options, name)
    code += structure(source, 'wifi_radio_twt_broadcast_t')
    code += '\nstatic wifi_radio_twt_broadcast_t **s_twt_broadcast;\n'
    code += extract(options, 'esp32_mquickjs_wifi_btwt_options_valid')
    code += BOUNDARIES
    code += BROADCAST_BOUNDARIES
    for name in ('wifi_radio_twt_individual_lease_retained', 'wifi_radio_twt_individual_find',
                 'esp32_mquickjs_wifi_radio_twt_individual_submit', 'esp32_mquickjs_wifi_radio_twt_individual_status',
                 'esp32_mquickjs_wifi_radio_twt_individual_tokens', 'esp32_mquickjs_wifi_radio_twt_individual_request_close',
                 'wifi_radio_twt_individual_retire', 'esp32_mquickjs_wifi_radio_twt_individual_close',
                 'wifi_radio_twt_individual_information', 'esp32_mquickjs_wifi_radio_twt_individual_suspend',
                 'esp32_mquickjs_wifi_radio_twt_individual_resume', 'wifi_radio_twt_broadcast_lease_retained',
                 'wifi_radio_twt_broadcast_find', 'esp32_mquickjs_wifi_radio_twt_broadcast_submit',
                 'esp32_mquickjs_wifi_radio_twt_broadcast_status', 'esp32_mquickjs_wifi_radio_twt_broadcast_tokens',
                 'esp32_mquickjs_wifi_radio_twt_broadcast_request_close', 'wifi_radio_twt_broadcast_retire',
                 'esp32_mquickjs_wifi_radio_twt_broadcast_close', 'esp32_mquickjs_wifi_radio_twt_close_agreements',
                 'esp32_mquickjs_wifi_radio_twt_close_pending'):
        code += extract(source, name)
    return code


class WiFiTwtAgreementRadio(unittest.TestCase):
    def test_multiple_owners_early_status_exact_close_and_failed_suffix(self):
        compile_run(self, agreement_radio_code() + MAIN)


BOUNDARIES = r'''
#define MALLOC_CAP_8BIT 1
#define MALLOC_CAP_INTERNAL 2
static bool allocation_fails,early_individual,accept_individual=true;
static unsigned individual_submits,teardowns,retire_steps;
static int individual_error,retirement_error;
static int32_t last_request=-1;
static esp32_mquickjs_wifi_twt_setup_result_t individual_results[8];
/* Only this SDK boundary value is injected, not an alternate Radio machine. */
typedef struct {int16_t individual_ids[8];uint32_t broadcast_id_bitmap;} esp32_mquickjs_wifi_twt_sdk_snapshot_t;
static esp32_mquickjs_wifi_twt_sdk_snapshot_t native_individual;
static void *heap_caps_calloc(size_t n,size_t size,unsigned caps) {
    assert(locks && !critical && caps==3);return allocation_fails?NULL:calloc(n,size);
}
void esp32_mquickjs_wifi_twt_setup_results_snapshot(esp32_mquickjs_wifi_twt_setup_results_snapshot_t *out) {
    assert(!critical);*out=(esp32_mquickjs_wifi_twt_setup_results_snapshot_t){.last_request_id=last_request};
}
void esp32_mquickjs_wifi_twt_setup_result_request_close(uint32_t id) {
    assert(!critical);
    for(unsigned i=0;i<8;++i)if(id && individual_results[i].identity==id)
        individual_results[i].flags|=ESP32_MQUICKJS_WIFI_TWT_SETUP_CLOSE_REQUESTED;
}
esp_err_t esp32_mquickjs_wifi_twt_setup_result_read(uint32_t id,esp32_mquickjs_wifi_twt_setup_result_t *out) {
    assert(!critical);
    for(unsigned i=0;i<8;++i)if(individual_results[i].identity==id){*out=individual_results[i];return 0;}
    return ESP_ERR_INVALID_STATE;
}
esp_err_t esp32_mquickjs_wifi_twt_sdk_individual_submit(const wifi_itwt_setup_config_t *cfg,
    esp32_mquickjs_wifi_twt_setup_dispatch_t *out) {
    assert(locks && !critical && cfg->twt_id>last_request);++individual_submits;
    if(early_individual) {
        esp32_mquickjs_wifi_twt_token_t tokens[8];unsigned count=esp32_mquickjs_wifi_radio_twt_individual_tokens(tokens,8,false);
        esp32_mquickjs_wifi_twt_individual_radio_state_t state;bool found=false;assert(count);
        for(unsigned i=0;i<count;++i) {
            assert(esp32_mquickjs_wifi_radio_twt_individual_status(&tokens[i],&state));
            if(state.dispatching && state.requested.twt_id==cfg->twt_id)found=true;
        }
        assert(found);
    }
    if(!accept_individual)return individual_error;
    last_request=cfg->twt_id;
    for(unsigned i=0;i<8;++i)if(!individual_results[i].identity){
        *out=(esp32_mquickjs_wifi_twt_setup_dispatch_t){.config=*cfg,.identity=(uint32_t)last_request+1,.sdk_error=individual_error};
        individual_results[i]=(esp32_mquickjs_wifi_twt_setup_result_t){.identity=out->identity,.request_id=last_request,
            .flags=ESP32_MQUICKJS_WIFI_TWT_SETUP_SUBMITTED};return individual_error;
    }
    assert(0);return ESP_ERR_NO_MEM;
}
esp_err_t esp32_mquickjs_wifi_twt_sdk_snapshot(esp32_mquickjs_wifi_twt_sdk_snapshot_t *out) {
    assert(locks && !critical);*out=native_individual;return 0;
}
esp_err_t esp32_mquickjs_wifi_twt_sdk_setup_teardown(uint32_t id,uint8_t flow) {
    assert(locks && !critical);++teardowns;
    for(unsigned i=0;i<8;++i)if(individual_results[i].identity==id){
        assert(native_individual.individual_ids[flow]==individual_results[i].request_id);
        assert(!(individual_results[i].flags&ESP32_MQUICKJS_WIFI_TWT_TEARDOWN_ATTEMPTED));
        individual_results[i].flags|=ESP32_MQUICKJS_WIFI_TWT_TEARDOWN_ATTEMPTED;return 0;
    }
    assert(0);return ESP_ERR_INVALID_STATE;
}
esp_err_t esp32_mquickjs_wifi_twt_setup_retire_poll(esp32_mquickjs_wifi_twt_setup_retire_t *state,
    const esp32_mquickjs_wifi_twt_token_t *token,uint32_t id) {
    assert(locks && !critical && token->identity);++retire_steps;state->stage="injected-retirement";
    if(retirement_error)return retirement_error;
    for(unsigned i=0;i<8;++i)if(individual_results[i].identity==id)individual_results[i].identity=0;
    state->result_released=state->released=true;state->stage=NULL;return 0;
}
'''
MAIN = r'''
static unsigned individual_owners(void) {return s_radio.clients[ESP32_MQUICKJS_WIFI_RADIO_CLIENT_TWT];}
static void individual_reset(void) {
    reset_vendor();s_radio.effective_mode=WIFI_MODE_STA;s_radio.started=true;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTED;
    s_radio.next_operation_identity=1;s_twt_identity=(esp32_mquickjs_wifi_twt_identity_t){.next_identity=1};
    memset(native_individual.individual_ids,0xff,sizeof(native_individual.individual_ids));
}
int main(void) {
    individual_reset();
    esp32_mquickjs_wifi_itwt_options_t options={.config={.setup_cmd=TWT_REQUEST,.min_wake_dura=1,
        .wake_invl_mant=10256,.timeout_time_ms=100},.timeout_ms=1};
    esp32_mquickjs_wifi_twt_token_t tokens[9]={0},old;
    allocation_fails=true;
    assert(esp32_mquickjs_wifi_radio_twt_individual_submit(&options,&tokens[0])==ESP_ERR_NO_MEM);
    assert(!individual_submits && !individual_owners());allocation_fails=false;early_individual=true;
    for(unsigned i=0;i<8;++i)assert(!esp32_mquickjs_wifi_radio_twt_individual_submit(&options,&tokens[i]));
    assert(individual_owners()==8 && s_radio.clients[ESP32_MQUICKJS_WIFI_RADIO_CLIENT_TWT]==8 && !s_radio.operation.identity);
    assert(esp32_mquickjs_wifi_radio_twt_individual_submit(&options,&tokens[8])==ESP_ERR_NO_MEM && individual_submits==8);
    esp32_mquickjs_wifi_radio_lease_t borrowed=s_twt_individual[0].lease;
    wifi_radio_operation_lock();wifi_radio_release_locked(&borrowed);wifi_radio_operation_unlock();assert(borrowed.acquired);
    old=tokens[0];++old.generation;assert(esp32_mquickjs_wifi_radio_twt_individual_close(&old)==ESP_ERR_INVALID_STATE);
    native_individual.individual_ids[3]=individual_results[0].request_id;
    retirement_error=ESP_ERR_NOT_FINISHED;
    assert(esp32_mquickjs_wifi_radio_twt_individual_close(&tokens[0])==ESP_ERR_NOT_FINISHED && teardowns==1 && individual_owners()==8);
    assert(esp32_mquickjs_wifi_radio_twt_individual_close(&tokens[0])==ESP_ERR_NOT_FINISHED && teardowns==1);
    esp32_mquickjs_wifi_twt_individual_radio_state_t state;
    assert(esp32_mquickjs_wifi_radio_twt_individual_status(&tokens[0],&state) && state.closing && state.cleanup_error==ESP_ERR_NOT_FINISHED);
    old=tokens[0];retirement_error=0;assert(!esp32_mquickjs_wifi_radio_twt_individual_close(&tokens[0]) && !tokens[0].identity && individual_owners()==7);
    assert(esp32_mquickjs_wifi_radio_twt_individual_close(&old)==ESP_ERR_INVALID_STATE);
    assert(!esp32_mquickjs_wifi_radio_twt_individual_submit(&options,&tokens[0]) && tokens[0].identity>old.identity);
    assert(!esp32_mquickjs_wifi_radio_twt_individual_request_close(&old));
    for(unsigned i=0;i<8;++i)assert(!esp32_mquickjs_wifi_radio_twt_individual_close(&tokens[i]));
    assert(!individual_owners());accept_individual=false;individual_error=77;
    assert(esp32_mquickjs_wifi_radio_twt_individual_submit(&options,&tokens[0])==77 && tokens[0].identity);
    unsigned before=retire_steps;assert(!esp32_mquickjs_wifi_radio_twt_individual_close(&tokens[0]) && retire_steps==before);
    options.connection_id_set=true;options.config.twt_id=0;
    assert(esp32_mquickjs_wifi_radio_twt_individual_submit(&options,&tokens[0])==ESP_ERR_INVALID_STATE);
    options.connection_id_set=false;s_twt_identity.next_connection_id=32768;
    assert(esp32_mquickjs_wifi_radio_twt_individual_submit(&options,&tokens[0])==ESP_ERR_NO_MEM);
    assert(!locks && !critical && !individual_owners());free(s_twt_individual);return 0;
}
'''

BOUNDARIES += r'''
static esp_err_t information_retire_error;
esp_err_t esp32_mquickjs_wifi_twt_sdk_information_reap(uint32_t id){(void)id;assert(!critical);return information_retire_error;}
esp_err_t esp32_mquickjs_wifi_twt_sdk_information_submit(uint32_t id,uint32_t ms,bool resume,uint32_t *out){
    (void)id;(void)ms;(void)resume;(void)out;assert(0);return ESP_ERR_INVALID_STATE;
}
bool esp32_mquickjs_wifi_twt_information_pending(void){return false;}
bool esp32_mquickjs_wifi_twt_information_read(uint32_t id,esp32_mquickjs_wifi_twt_information_result_t *out){
    (void)id;(void)out;assert(0);return false;
}
void esp32_mquickjs_wifi_twt_information_abandon(uint32_t id){(void)id;assert(0);}
'''

BROADCAST_BOUNDARIES = r'''
static esp32_mquickjs_wifi_btwt_timer_result_t broadcast_results[32];
static esp32_mquickjs_wifi_twt_teardown_tx_snapshot_t broadcast_teardown;
static uint16_t broadcast_attempts[32];
static unsigned broadcast_submits, broadcast_teardowns, broadcast_retires;
static esp_err_t broadcast_submit_error, broadcast_retire_error, broadcast_teardown_error;
static bool broadcast_accept=true, broadcast_early;
static uint32_t broadcast_sequence=1000;
static void heap_caps_free(void *p) {assert(!critical);free(p);}
uint16_t esp32_mquickjs_wifi_twt_tx_broadcast_remaining(unsigned slot) {
    assert(!critical && slot<32);return 255-broadcast_attempts[slot];
}
bool esp32_mquickjs_wifi_btwt_setup_result(unsigned slot,uint32_t identity,esp32_mquickjs_wifi_btwt_timer_result_t *out) {
    assert(!critical);if(broadcast_results[slot].identity!=identity)return false;
    *out=broadcast_results[slot];return true;
}
esp_err_t esp32_mquickjs_wifi_twt_sdk_broadcast_submit(const wifi_btwt_setup_config_t *config,
    esp32_mquickjs_wifi_btwt_dispatch_t *out) {
    assert(locks && !critical);++broadcast_submits;
    if(broadcast_early) {
        esp32_mquickjs_wifi_twt_token_t tokens[31];
        unsigned count=esp32_mquickjs_wifi_radio_twt_broadcast_tokens(tokens,31,false);
        bool found=false;
        for(unsigned i=0;i<count;++i) {
            esp32_mquickjs_wifi_twt_broadcast_radio_state_t state;
            assert(esp32_mquickjs_wifi_radio_twt_broadcast_status(&tokens[i],&state));
            if(state.requested.btwt_id==config->btwt_id)found=state.dispatching;
        }
        assert(found);
    }
    if(!broadcast_accept)return broadcast_submit_error;
    ++broadcast_attempts[config->btwt_id];
    *out=(esp32_mquickjs_wifi_btwt_dispatch_t){.config=*config,.identity=++broadcast_sequence,
        .native_entered=true,.native_completed=true,.driver_called=true,.sdk_error=broadcast_submit_error};
    broadcast_results[config->btwt_id]=(esp32_mquickjs_wifi_btwt_timer_result_t){.identity=out->identity,.held=true};
    return broadcast_submit_error;
}
void esp32_mquickjs_wifi_twt_teardown_tx_snapshot(esp32_mquickjs_wifi_twt_teardown_tx_snapshot_t *out) {
    assert(!critical);*out=broadcast_teardown;
}
esp_err_t esp32_mquickjs_wifi_twt_sdk_broadcast_teardown(unsigned slot,uint32_t identity) {
    assert(locks && !critical && broadcast_results[slot].identity==identity);++broadcast_teardowns;
    broadcast_teardown=(esp32_mquickjs_wifi_twt_teardown_tx_snapshot_t){.broadcast=true,.flow=slot,.identity=identity};
    return broadcast_teardown_error;
}
esp_err_t esp32_mquickjs_wifi_btwt_retire_poll(esp32_mquickjs_wifi_btwt_retire_t *state,
    const esp32_mquickjs_wifi_twt_token_t *token,unsigned slot,uint32_t identity) {
    assert(locks && !critical && token->identity && broadcast_results[slot].identity==identity);
    ++broadcast_retires;state->stage="injected-broadcast-retirement";
    /* Real owner must hide an in-progress mutable retirement record. */
    esp32_mquickjs_wifi_twt_broadcast_radio_state_t out;
    assert(!esp32_mquickjs_wifi_radio_twt_broadcast_status(token,&out));
    if(broadcast_retire_error)return broadcast_retire_error;
    broadcast_results[slot].held=false;state->released=true;state->stage=NULL;
    native_individual.broadcast_id_bitmap &= ~(1U<<slot);return 0;
}
'''
