"""Deferred production native cancellation wrappers with injected SDK record.

Host pointer width differs: this checks wrapper decisions and no delegation on
mismatch. The pinned layout assertions and linker wrapping need target evidence;
SDK task serialization and RF cancellation require the later device stage.
"""
import unittest
from test_wifi_config_controls import sdk_types
from test_wifi_driver_phy import COMPONENT
from test_wifi_action_lane import PRELUDE
from test_wireless_control_regression import compile_run
from wireless_vm_fixture import extract


class WiFiActionSdk(unittest.TestCase):
    def test_nan_release_runs_in_native_dispatch_and_returns_exact_snapshot(self):
        source = (COMPONENT / 'src/modules/wifi_action/esp32_mquickjs_wifi_action_sdk.c').read_text()
        ndp = (COMPONENT / 'internal/esp32_mquickjs_wifi_nan_ndp.h').read_text()
        code = PRELUDE + '#define CONFIG_ESP_WIFI_NAN_SYNC_ENABLE 1\n'
        code += sdk_types('esp32c5/representative', ('wifi_action_tx_req_t', 'wifi_roc_req_t'))
        code += ndp[ndp.index('typedef struct {'):ndp.index('} esp32_mquickjs_wifi_nan_ndp_status_t;') +
                    len('} esp32_mquickjs_wifi_nan_ndp_status_t;')] + '\n'
        start = source.index('#define ACTION_SDK_NAN_RELEASE_TYPE')
        code += source[start:source.index('\n#endif', start)] + BOUNDARIES + NAN_BOUNDARY
        for name in ('esp32_mquickjs_wifi_action_receive', 'action_sdk_owner', 'action_sdk_guarded_callback',
                     '__wrap_wifi_action_tx_process', 'esp32_mquickjs_wifi_nan_sdk_ndp_release'):
            code += extract(source, name)
        compile_run(self, code + NAN_MAIN)

    def test_cancel_checks_native_owner_in_ioctl_context(self):
        for target in ('esp32c3/representative', 'esp32s3/representative-psram', 'esp32c5/representative'):
            with self.subTest(target=target):
                code = PRELUDE + sdk_types(target, ('wifi_action_tx_req_t', 'wifi_roc_req_t')) + BOUNDARIES
                source = (COMPONENT / 'src/modules/wifi_action/esp32_mquickjs_wifi_action_sdk.c').read_text()
                for name in ('esp32_mquickjs_wifi_action_receive', 'action_sdk_owner', 'action_sdk_guarded_callback',
                             '__wrap_wifi_action_tx_process', '__wrap_wifi_roc_process'):
                    code += extract(source, name)
                compile_run(self, code + MAIN)


BOUNDARIES = r'''
#define ACTION_SDK_FENCE_TYPE ((wifi_action_tx_t)INT32_MAX)
#define ACTION_SDK_QUIESCENT_TYPE ((wifi_action_tx_t)(INT32_MAX - 1))
#define ESP_ERR_TIMEOUT 99
static uint8_t g_offchan_ctx[28];
static unsigned delegates;static bool ioctl_task;
static esp_err_t __real_wifi_action_tx_process(void *message) {
    assert(message && ioctl_task);++delegates;return 77;
}
static esp_err_t __real_wifi_roc_process(void *message) {
    assert(message && ioctl_task);++delegates;return 88;
}
static int other_receive(uint8_t *a,uint8_t *b,size_t c,uint8_t d){(void)a;(void)b;(void)c;(void)d;return 0;}
'''
NAN_BOUNDARY = r'''
static unsigned nan_releases,ipc_posts;
static int ipc_error,nan_error;
static esp_err_t esp_wifi_action_tx_req(wifi_action_tx_req_t *request);
static esp_err_t esp32_mquickjs_wifi_nan_sdk_ndp_release_native(uint32_t id,
 esp32_mquickjs_wifi_nan_ndp_status_t *status){
 assert(ioctl_task&&id==19);++nan_releases;
 *status=(esp32_mquickjs_wifi_nan_ndp_status_t){.identity=id,.native_deleted=true,.frames_retired=true};
 return nan_error;
}
'''
NAN_MAIN = r'''
static esp_err_t esp_wifi_action_tx_req(wifi_action_tx_req_t *request){
 assert(!ioctl_task&&request->ifx==WIFI_IF_NAN);++ipc_posts;
 if(ipc_error)return ipc_error;
 uint8_t message[32]={0};memcpy(message+20,&request,sizeof(request));
 ioctl_task=true;esp_err_t error=__wrap_wifi_action_tx_process(message);ioctl_task=false;return error;
}
int main(void){
 esp32_mquickjs_wifi_nan_ndp_status_t state={.identity=123};
 assert(esp32_mquickjs_wifi_nan_sdk_ndp_release(0,&state)==ESP_ERR_INVALID_ARG&&!ipc_posts);
 ipc_error=-72;
 assert(esp32_mquickjs_wifi_nan_sdk_ndp_release(19,&state)==-72&&!nan_releases&&state.identity==123);
 ipc_error=0;nan_error=-73;
 assert(esp32_mquickjs_wifi_nan_sdk_ndp_release(19,&state)==-73&&nan_releases==1&&state.identity==123);
 nan_error=0;
 assert(!esp32_mquickjs_wifi_nan_sdk_ndp_release(19,&state)&&nan_releases==2);
 assert(state.identity==19&&state.native_deleted&&state.frames_retired&&!delegates);
 action_sdk_nan_release_t wrong={.request={.ifx=WIFI_IF_STA,.type=ACTION_SDK_NAN_RELEASE_TYPE,
  .rx_cb=esp32_mquickjs_wifi_action_receive},.identity=19};
 uint8_t message[32]={0};void *request=&wrong;memcpy(message+20,&request,sizeof(request));
 ioctl_task=true;assert(__wrap_wifi_action_tx_process(message)==ESP_ERR_INVALID_ARG&&nan_releases==2);
 /* Exercise unchanged external delegation boundaries as well. */
 assert(__real_wifi_roc_process(message)==88);
 assert(!other_receive(NULL,NULL,0,0));
}
'''
MAIN = r'''
static void owner(wifi_action_rx_cb_t callback,unsigned interface,unsigned channel,unsigned secondary,unsigned id) {
    memset(g_offchan_ctx,0,sizeof(g_offchan_ctx));
    uint32_t context=(uint32_t)(uintptr_t)callback;
    memcpy(g_offchan_ctx+4,&context,4);memcpy(g_offchan_ctx+8,&interface,4);
    memcpy(g_offchan_ctx+20,&secondary,4);g_offchan_ctx[2]=channel;g_offchan_ctx[12]=id;
}
int main(void) {
    uint8_t message[32]={0};ioctl_task=true;
    wifi_action_tx_req_t action={.type=WIFI_OFFCHAN_TX_CANCEL,.rx_cb=esp32_mquickjs_wifi_action_receive,
        .ifx=WIFI_IF_STA,.channel=6,.sec_channel=WIFI_SECOND_CHAN_ABOVE,.op_id=255};
    wifi_roc_req_t roc={.type=WIFI_ROC_CANCEL,.rx_cb=esp32_mquickjs_wifi_action_receive,
        .ifx=WIFI_IF_AP,.channel=6,.sec_channel=WIFI_SECOND_CHAN_NONE,.op_id=0};
    assert(__wrap_wifi_action_tx_process(NULL)==ESP_ERR_INVALID_ARG && !delegates);
    assert(__wrap_wifi_roc_process(message)==ESP_ERR_INVALID_ARG && !delegates);
    for(unsigned kind=0;kind<2;++kind){
        void *request=kind?(void *)&roc:(void *)&action;memcpy(message+20,&request,sizeof(request));
        for(unsigned mismatch=0;mismatch<6;++mismatch){
            owner(esp32_mquickjs_wifi_action_receive,kind?WIFI_IF_AP:WIFI_IF_STA,6,
                kind?WIFI_SECOND_CHAN_NONE:WIFI_SECOND_CHAN_ABOVE,kind?0:255);
            if(mismatch==0)g_offchan_ctx[4]^=1;
            if(mismatch==1)g_offchan_ctx[8]^=1;
            if(mismatch==2)g_offchan_ctx[2]^=1;
            if(mismatch==3)g_offchan_ctx[20]^=1;
            if(mismatch==4)g_offchan_ctx[12]^=1;
            if(mismatch==5)memset(g_offchan_ctx,0,sizeof(g_offchan_ctx));
            unsigned before=delegates;
            assert((kind?__wrap_wifi_roc_process(message):__wrap_wifi_action_tx_process(message))==ESP_ERR_INVALID_STATE);
            assert(delegates==before);
        }
        owner(esp32_mquickjs_wifi_action_receive,kind?WIFI_IF_AP:WIFI_IF_STA,6,
            kind?WIFI_SECOND_CHAN_NONE:WIFI_SECOND_CHAN_ABOVE,kind?0:255);
        unsigned before=delegates;
        assert((kind?__wrap_wifi_roc_process(message):__wrap_wifi_action_tx_process(message))==(kind?88:77));
        assert(delegates==before+1);
        /* Existing native modules and normal submit remain delegated. */
        memset(g_offchan_ctx,0,sizeof(g_offchan_ctx));
        if(kind)roc.rx_cb=other_receive;else action.rx_cb=other_receive;
        assert((kind?__wrap_wifi_roc_process(message):__wrap_wifi_action_tx_process(message))==(kind?88:77));
        if(kind){roc.rx_cb=esp32_mquickjs_wifi_action_receive;roc.type=WIFI_ROC_REQ;}
        else{action.rx_cb=esp32_mquickjs_wifi_action_receive;action.type=WIFI_OFFCHAN_TX_REQ;}
        assert((kind?__wrap_wifi_roc_process(message):__wrap_wifi_action_tx_process(message))==(kind?88:77));
    }
    /* Private fence is acknowledged in this exact ioctl handler without
     * inspecting owner or delegating a driver mutation. */
    void *request=&action;memcpy(message+20,&request,sizeof(request));
    action.rx_cb=esp32_mquickjs_wifi_action_receive;action.type=ACTION_SDK_FENCE_TYPE;
    unsigned before=delegates;assert(__wrap_wifi_action_tx_process(message)==ESP_OK && delegates==before);
    action.type=ACTION_SDK_QUIESCENT_TYPE;memset(g_offchan_ctx,0,sizeof(g_offchan_ctx));
    assert(__wrap_wifi_action_tx_process(message)==ESP_OK && delegates==before);
    /* Every byte matters, including pending identity/channel and flags that
     * are not part of the two in-progress booleans. Foreign state also blocks. */
    for(unsigned i=0;i<sizeof(g_offchan_ctx);++i) {
        g_offchan_ctx[i]=1;
        assert(__wrap_wifi_action_tx_process(message)==ESP_ERR_TIMEOUT && delegates==before);
        g_offchan_ctx[i]=0;
    }
    action.rx_cb=other_receive;assert(__wrap_wifi_action_tx_process(message)==77 && delegates==before+1);
    return 0;
}
'''
