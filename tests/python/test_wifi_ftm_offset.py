"""Deferred real FTM offset ledger/Radio admission and restart suffix fixtures.

Only SDK, lock and storage boundaries are injected. No alternative lifecycle
implementation, SDK IPC execution or RF proof. AST parse only until Wi-Fi stage.
"""
import re
import tempfile
import unittest
from test_wifi_driver_policy import policy_code
from test_wifi_driver_phy import COMPONENT
from test_wifi_policy_record import PRELUDE
from test_wifi_rx_target import unit
from test_wireless_control_regression import compile_run
from wireless_vm_fixture import CORE, build, extract, run

GATES = '\n#define CONFIG_ESP_WIFI_FTM_ENABLE 1\n#define CONFIG_ESP_WIFI_FTM_RESPONDER_SUPPORT 1\n'


def ledger_code():
    return unit(COMPONENT / 'internal/esp32_mquickjs_wifi_ftm_offset.h') + unit(
        COMPONENT / 'src/modules/wifi_ftm/esp32_mquickjs_wifi_ftm_offset.c')


def offset_code(profile):
    code = GATES + policy_code(profile, True, False)
    code = code.replace('struct {unsigned identity;} operation,lifecycle;',
                        'struct {unsigned identity,generation;} operation,lifecycle;')
    code += ledger_code()
    source = (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
    code += 'static esp32_mquickjs_wifi_ftm_offset_state_t s_ftm_offset;\n'
    code += re.search(r'static struct \{[^}]*\} s_policy_restart;', source).group(0)
    code += RADIO_BOUNDARIES
    for name in ('wifi_radio_ftm_offset_writer', 'esp32_mquickjs_wifi_radio_ftm_offset_status',
                 'esp32_mquickjs_wifi_radio_write_ftm_offset', 'wifi_radio_policy_restart_prepare_locked',
                 'wifi_radio_policy_restart_replay_locked'):
        code += extract(source, name)
    return code


class WiFiFtmOffset(unittest.TestCase):
    def test_production_mutation_boundary_preserves_only_reviewed_target_stop_payloads(self):
        source = (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
        boundary = (COMPONENT / 'internal/esp32_mquickjs_wifi_radio_mutation.h').read_text()
        for target in ('ESP32C3', 'ESP32S3', 'ESP32C5', 'UNREVIEWED'):
            code = '#include <stdbool.h>\n#include <stdint.h>\n#include <assert.h>\n'
            code += '#define CONFIG_IDF_TARGET_' + target + ' 1\n'
            code += 'static struct {bool unchanged;} s_stop_snapshot;\n'
            code += 'static int native;static int esp_wifi_ftm_resp_set_offset(int16_t n){native=n;return 77;}\n'
            code += extract(source, 'wifi_radio_invalidate_stop_snapshot_locked') + boundary
            expected = 'false' if target == 'UNREVIEWED' else 'true'
            code += 'int main(void){s_stop_snapshot.unchanged=true;assert(esp_wifi_ftm_resp_set_offset(-7)==77);'
            code += 'assert(native==-7 && s_stop_snapshot.unchanged==' + expected + ');return 0;}\n'
            compile_run(self, code)

    def test_real_ledger_signed_values_failure_history_replay_and_exhaustion(self):
        compile_run(self, PRELUDE + GATES + '\n#define CONFIG_ESP_WIFI_SOFTAP_SUPPORT 1\n' + ledger_code() + LEDGER_MAIN)

    def test_real_radio_admission_snapshot_exact_lifecycle_and_prestart_replay(self):
        for profile in ('esp32c3/representative', 'esp32s3/representative-psram', 'esp32c5/representative'):
            with self.subTest(profile=profile):
                code = offset_code(profile)
                compile_run(self, code + RADIO_MAIN)

    def test_public_signed_capture_status_errors_and_nth_gc_allocation(self):
        source = (COMPONENT / 'src/modules/wifi_ftm/esp32_mquickjs_wifi_ftm_module.c').read_text()
        code = re.sub(r'\bcalls\b', 'sdk_calls', offset_code('esp32c5/representative'))
        code = re.sub(r'\bfail_at\b', 'sdk_fail_at', code)
        code += unit(CORE / 'esp32_mquickjs_options.c')
        code += extract((CORE / 'esp32_mquickjs.c').read_text(), 'esp32_mquickjs_throw_native_error')
        code += 'static const char *esp_err_to_name(int error){(void)error;return "injected";}\n'
        code += re.search(r'^#define SET\(.*$', source, re.M).group(0) + '\n'
        for name in ('ftm_offset_to_js', 'js_wifi_ftm_responder_offset_status', 'js_wifi_ftm_set_responder_offset'):
            code += extract(source, name)
        with tempfile.TemporaryDirectory() as tmp:
            binary = build(tmp, code, VM_MAIN)
            for scenario in ('-32768', '32767', '0', '-32769', '32768', '1.5', 'NaN', 'Infinity', '"1"', 'null', 'arity', 'sdk-error', 'status'):
                with self.subTest(scenario=scenario):run([str(binary), scenario])


LEDGER_MAIN = r'''
static esp32_mquickjs_wifi_ftm_offset_state_t state;
static unsigned calls;
static int failure;
static int16_t native;
static int writer(void *opaque, int16_t value) {
    assert(opaque == &state && state.uncertain && !state.known);
    assert(state.requested_cm == value); ++calls; native = value;
    return failure; /* Mutation may precede error. */
}
int main(void) {
    bool attempted,completed=false;
    esp32_mquickjs_wifi_ftm_offset_snapshot_t snapshot;
#define APPLY(gen,value) esp32_mquickjs_wifi_ftm_offset_apply(&state,gen,value,writer,&state,&attempted)
#define CAPTURE(gen) esp32_mquickjs_wifi_ftm_offset_capture(&state,gen,&snapshot)
#define REPLAY(gen) esp32_mquickjs_wifi_ftm_offset_replay(&state,gen,&snapshot,&completed,writer,&state)
    assert(CAPTURE(1)==ESP_OK && !snapshot.configured && !calls);
    assert(REPLAY(2)==ESP_OK && completed && !calls);completed=false;
    assert(APPLY(0,0)==ESP_ERR_INVALID_ARG && !attempted && !calls);
    assert(APPLY(1,INT16_MIN)==ESP_OK && native==INT16_MIN && state.known && state.accepted_revision==1);
    failure=77;assert(APPLY(1,INT16_MAX)==77 && native==INT16_MAX && state.uncertain && !state.known);
    assert(state.accepted_cm==INT16_MIN && state.accepted_revision==1 && state.revision==2);
    assert(CAPTURE(1)==ESP_ERR_INVALID_STATE && !snapshot.configured);
    assert(APPLY(2,0)==ESP_ERR_INVALID_STATE && calls==2);
    failure=0;assert(APPLY(1,0)==ESP_OK && state.known && state.accepted_cm==0);
    assert(CAPTURE(1)==ESP_OK && snapshot.configured && snapshot.centimeters==0);
    assert(REPLAY(1)==ESP_ERR_INVALID_STATE && !completed);
    esp32_mquickjs_wifi_ftm_offset_invalidate(&state);
    assert(!state.known && !state.uncertain && state.configured && state.accepted_revision==3);
    assert(REPLAY(2)==ESP_OK && completed && state.generation==2 && native==0);
    unsigned before=calls;assert(REPLAY(2)==ESP_OK && calls==before);
    esp32_mquickjs_wifi_ftm_offset_invalidate(&state);
    assert(REPLAY(2)==ESP_ERR_INVALID_STATE && calls==before);
    completed=false;failure=88;assert(REPLAY(3)==88 && !completed && state.uncertain);
    failure=0;assert(REPLAY(3)==ESP_OK && completed && state.known);
    assert(CAPTURE(2)==ESP_ERR_INVALID_STATE);
    state.revision=UINT32_MAX-1;
    assert(APPLY(3,INT16_MAX)==ESP_OK && state.revision==UINT32_MAX && native==INT16_MAX);
    assert(CAPTURE(3)==ESP_ERR_INVALID_STATE);
    before=calls;assert(APPLY(3,1)==ESP_ERR_INVALID_STATE && !attempted && calls==before);
    esp32_mquickjs_wifi_ftm_offset_invalidate(&state);
    assert(APPLY(4,0)==ESP_ERR_INVALID_STATE && calls==before);
    return 0;
}
'''

RADIO_BOUNDARIES = r'''
static int offset_error,mode_error;static unsigned offset_calls,mode_calls;
static int16_t native_offset;static bool mode_mismatch;
static int esp_wifi_get_mode(wifi_mode_t *mode) {
    assert(locks && !critical);++mode_calls;
    *mode=mode_mismatch?WIFI_MODE_STA:s_radio.effective_mode;return mode_error;
}
static int esp_wifi_ftm_resp_set_offset(int16_t value) {
    assert(locks && !critical && !s_radio.started && s_ftm_offset.uncertain && !s_ftm_offset.known);
    ++offset_calls;native_offset=value;return offset_error;
}
'''

RADIO_MAIN = r'''
static void offset_reset(void) {
    policy_reset(true);s_radio.effective_mode=WIFI_MODE_APSTA;
    memset(&s_ftm_offset,0,sizeof(s_ftm_offset));memset(&s_policy_restart,0,sizeof(s_policy_restart));
    offset_error=mode_error=0;offset_calls=mode_calls=0;mode_mismatch=false;
}
int main(void) {
    esp32_mquickjs_wifi_ftm_offset_state_t record;
#define WRITE(v) esp32_mquickjs_wifi_radio_write_ftm_offset(v,&record,&result)
    offset_reset();assert(WRITE(-32769)==ESP_ERR_INVALID_ARG && !mode_calls && !offset_calls);
    assert(WRITE(32768)==ESP_ERR_INVALID_ARG && !mode_calls);
    assert(WRITE(-32768)==ESP_OK && native_offset==-32768 && record.known && result.mutation_attempted);
    assert(!locks && !critical && !helper_locks);
    offset_error=77;assert(WRITE(32767)==77 && native_offset==32767 && record.uncertain && record.accepted_cm==-32768);
    offset_error=0;assert(WRITE(0)==ESP_OK && record.known && !record.uncertain);
    unsigned before=offset_calls;
    s_radio.started=true;assert(WRITE(1)==ESP_ERR_INVALID_STATE && offset_calls==before);s_radio.started=false;
    s_radio.leases[2].identity=55;assert(WRITE(1)==ESP_ERR_INVALID_STATE && offset_calls==before);s_radio.leases[2].identity=0;
    s_radio.operation.identity=9;assert(WRITE(1)==ESP_ERR_INVALID_STATE);s_radio.operation.identity=0;
    s_radio.wake_locks=1;assert(WRITE(1)==ESP_ERR_INVALID_STATE);s_radio.wake_locks=0;
    s_radio.promiscuous_claimed=true;assert(WRITE(1)==ESP_ERR_INVALID_STATE);s_radio.promiscuous_claimed=false;
    mode_error=71;assert(WRITE(1)==71 && !result.mutation_attempted);mode_error=0;
    mode_mismatch=true;assert(WRITE(1)==ESP_ERR_INVALID_STATE && !result.mutation_attempted);mode_mismatch=false;
    esp32_mquickjs_wifi_radio_lifecycle_t token={.identity=41,.generation=s_radio.generation};
    s_radio.lifecycle.identity=token.identity;s_radio.lifecycle.generation=token.generation;
    assert(WRITE(1)==ESP_ERR_INVALID_STATE && offset_calls==before);
    wifi_radio_operation_lock();
    assert(wifi_radio_policy_restart_prepare_locked(&token,WIFI_MODE_STA)==ESP_ERR_INVALID_STATE && !s_policy_restart.owner.identity);
    assert(wifi_radio_policy_restart_prepare_locked(&token,WIFI_MODE_APSTA)==ESP_OK);
    assert(s_policy_restart.ftm_offset.configured && s_policy_restart.ftm_offset.centimeters==0);
    esp32_mquickjs_wifi_ftm_offset_invalidate(&s_ftm_offset);++s_radio.generation;
    offset_error=88;assert(wifi_radio_policy_restart_replay_locked(&token,false)==88);
    assert(!s_policy_restart.ftm_offset_completed && !strcmp(s_radio.fault_stage,"restart-ftm-offset"));
    assert(wifi_radio_policy_restart_prepare_locked(&token,WIFI_MODE_APSTA)==ESP_OK); /* frozen input survives failure */
    s_radio.fault_stage=NULL;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STOPPED;offset_error=0;
    assert(wifi_radio_policy_restart_replay_locked(&token,false)==ESP_OK && s_policy_restart.ftm_offset_completed);
    before=offset_calls;assert(wifi_radio_policy_restart_replay_locked(&token,false)==ESP_OK && offset_calls==before);
    s_radio.started=true;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTED;
    assert(wifi_radio_policy_restart_replay_locked(&token,true)==ESP_OK && offset_calls==before);
    assert(s_ftm_offset.known && s_ftm_offset.generation==s_radio.generation && native_offset==0);
    s_radio.started=false;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STOPPED;
    esp32_mquickjs_wifi_ftm_offset_invalidate(&s_ftm_offset);++s_radio.generation;
    assert(wifi_radio_policy_restart_replay_locked(&token,false)==ESP_OK && offset_calls==before+1);
    wifi_radio_operation_unlock();assert(!locks && !critical);
    return 0;
}
'''

VM_MAIN = r'''
int main(int argc,char **argv) {
    assert(argc==2);bool arity=!strcmp(argv[1],"arity"),failed=!strcmp(argv[1],"sdk-error"),status=!strcmp(argv[1],"status");
    bool valid=!strcmp(argv[1],"-32768") || !strcmp(argv[1],"32767") || !strcmp(argv[1],"0") || failed;
    int total=1;
    for(int nth=0;nth<=total;++nth) {
        void *heap=malloc(192*1024);JSContext *ctx=JS_NewContext(heap,192*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
        JSGCRef arg_ref,out_ref,field_ref;JSValue *arg=JS_PushGCRef(ctx,&arg_ref),*out=JS_PushGCRef(ctx,&out_ref),*field=JS_PushGCRef(ctx,&field_ref);
        const char *expr=arity || failed || status?"0":argv[1];
        *arg=JS_Eval(ctx,expr,strlen(expr),"offset",JS_EVAL_RETVAL);assert(!JS_IsException(*arg));
        policy_reset(true);s_radio.effective_mode=WIFI_MODE_APSTA;
        memset(&s_ftm_offset,0,sizeof(s_ftm_offset));offset_calls=mode_calls=0;mode_error=0;mode_mismatch=false;offset_error=failed?77:0;
        calls=0;fail_at=nth;collect=inject=true;
        if(status)*out=js_wifi_ftm_responder_offset_status(ctx,NULL,0,NULL);
        else *out=js_wifi_ftm_set_responder_offset(ctx,NULL,arity?0:1,arg);
        inject=collect=false;
        if(!nth){total=calls;assert(JS_IsException(*out)==!(status || (valid && !failed)));}
        assert(offset_calls==(valid?1U:0U));
        if(valid)assert(s_ftm_offset.known==!failed && s_ftm_offset.uncertain==failed);
        if(JS_IsException(*out)) {
            assert(JS_HasException(ctx));*out=JS_GetException(ctx);
            if(!nth && failed) {
                *out=JS_GetPropertyStr(ctx,*out,"details");*field=JS_GetPropertyStr(ctx,*out,"mutationAttempted");assert(*field==JS_TRUE);
                *out=JS_GetPropertyStr(ctx,*out,"offset");*field=JS_GetPropertyStr(ctx,*out,"uncertain");assert(*field==JS_TRUE);
            }
        } else {
            *field=JS_GetPropertyStr(ctx,*out,"valueCm");
            if(status)assert(JS_IsNull(*field));
            else {int32_t value;assert(!JS_ToInt32(ctx,&value,*field) && value==atoi(expr));}
        }
        if(valid && !failed) { /* Conversion failure must not undo SDK acceptance. */
            *out=js_wifi_ftm_responder_offset_status(ctx,NULL,0,NULL);assert(!JS_IsException(*out));
            *field=JS_GetPropertyStr(ctx,*out,"known");assert(*field==JS_TRUE && offset_calls==1);
        }
        JS_PopGCRef(ctx,&field_ref);JS_PopGCRef(ctx,&out_ref);JS_PopGCRef(ctx,&arg_ref);JS_FreeContext(ctx);
        assert(!root_count && !native_live && !locks && !critical && !helper_locks);free(heap);
    }
    return 0;
}
'''
