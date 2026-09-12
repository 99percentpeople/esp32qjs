"""Production RF ingress/TX dispatch with controlled Radio/SDK boundaries."""
import unittest
from test_wifi_scan_lifecycle import extract, ROOT
from test_wireless_control_regression import compile_run
CSI = ROOT/'components/esp32_mquickjs/src/modules/wifi_csi/esp32_mquickjs_wifi_csi.c'
NOW = ROOT/'components/esp32_mquickjs/src/modules/espnow/esp32_mquickjs_espnow.c'
COMMON = r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <string.h>
typedef int esp_err_t,wifi_second_chan_t;
typedef struct { bool acquired;uint32_t generation; } esp32_mquickjs_wifi_radio_lease_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE -2
#define ESP_ERR_ESPNOW_CHAN -3
#define WIFI_SECOND_CHAN_NONE 0
#define WIFI_CSI_RUNNING 1
#define WIFI_CSI_FAULTED 2
typedef int wifi_csi_lifecycle_t;
typedef struct { uint8_t primary;int secondary;uint32_t generation;bool fixed,conflicted; } esp32_mquickjs_wifi_radio_channel_status_t;
static bool conflict=true,fixed=true;static int observation_error;
static int esp32_mquickjs_wifi_radio_lease_channel_status(const esp32_mquickjs_wifi_radio_lease_t *lease,esp32_mquickjs_wifi_radio_channel_status_t *s) {
    (void)lease;*s=(esp32_mquickjs_wifi_radio_channel_status_t){.primary=11,.generation=2,.fixed=fixed,.conflicted=conflict};return observation_error;
}
static int esp32_mquickjs_wifi_radio_get_channel(uint8_t *p,int *s,uint32_t *g) { *p=11;*s=0;*g=2;return observation_error; }
'''
CSI_SDK = r'''
typedef struct { atomic_int invalid_callback_data; } counters_t;
typedef struct { counters_t counters;atomic_bool accepting,identity_exhausted; } resources_t;
#define ESP32_MQUICKJS_WIFI_CSI_PUBLISH_IDENTITY_EXHAUSTED 9
typedef struct { int channel;uint64_t timestamp_us;uint32_t radio_generation; } esp32_mquickjs_wifi_csi_metadata_t;
typedef struct { uint8_t *buf;unsigned len,payload_len;struct {unsigned sig_len;} rx_ctrl; } wifi_csi_info_t;
typedef struct {const uint8_t *bytes;size_t readable_bytes;} esp32_mquickjs_wifi_csi_rx_packet_t;
typedef struct {const uint8_t *bytes;size_t copied_length;uint32_t driver_packet_length,driver_payload_length;} esp32_mquickjs_wifi_csi_packet_input_t;
static bool esp32_mquickjs_wifi_csi_rx_native_take(const wifi_csi_info_t *info,esp32_mquickjs_wifi_csi_rx_packet_t *packet){
    (void)info;memset(packet,0,sizeof(*packet));return false;
}
typedef struct {
    esp32_mquickjs_wifi_radio_lease_t radio_lease;atomic_int lifecycle;atomic_bool channel_conflicted;
    resources_t *resources;resources_t storage;struct { int capture;bool fixed_channel;uint8_t channel; } options;
    uint8_t effective_channel;int effective_secondary;unsigned radio_generation;
    int last_error;const char *last_error_code,*last_error_stage;
} wifi_csi_session_t;
static unsigned published,leaves;
static uint64_t published_time;static uint32_t published_generation;
static int64_t esp_timer_get_time(void) { return INT64_C(0x300000064); }
static bool esp32_mquickjs_wifi_csi_callback_enter(resources_t *r) { return atomic_load(&r->accepting); }
static void esp32_mquickjs_wifi_csi_callback_leave(resources_t *r) { (void)r;leaves++; }
static void esp32_mquickjs_wifi_csi_resources_set_accepting(resources_t *r,bool v) { atomic_store(&r->accepting,v); }
static void esp32_mquickjs_wifi_csi_target_normalize_metadata(void *i,void *c,void *m) { (void)i;(void)c;((esp32_mquickjs_wifi_csi_metadata_t *)m)->channel=11; }
static int wifi_csi_publish_event;
static int esp32_mquickjs_wifi_csi_callback_publish(resources_t *r,void *m,const uint8_t *b,unsigned len,const esp32_mquickjs_wifi_csi_packet_input_t *packet,int f,void *p) { (void)r;(void)b;(void)len;(void)packet;(void)f;(void)p;esp32_mquickjs_wifi_csi_metadata_t *meta=m;published_time=meta->timestamp_us;published_generation=meta->radio_generation;published++;return 0; }
'''
TX_SDK = r'''
#define portENTER_CRITICAL(p) ((void)(p))
#define portEXIT_CRITICAL(p) ((void)(p))
#define ESP32_MQUICKJS_ESPNOW_TX_SLOT_NONE 255
typedef struct state { uint8_t address[6],*payload;unsigned payload_length;atomic_bool submitted,completed;int err;int64_t completed_at_us;void *runtime;int token; } esp32_mquickjs_future_driver_state_t;
typedef struct { unsigned length;uint8_t address[6]; } espnow_tx_packet_t;
typedef struct {
    esp32_mquickjs_wifi_radio_lease_t radio_lease;int lock,tx_queue;uint8_t tx_staging[8],*tx_payloads;unsigned max_payload_bytes;
    espnow_tx_packet_t *tx_packets;atomic_uint active_queued_slot;
    atomic_bool queued_send_delivered,queued_send_completed;atomic_int queued_send_error;
    atomic_llong queued_send_started_us;
    atomic_uint sent_packets,sent_bytes,send_failures;
    _Atomic(esp32_mquickjs_future_driver_state_t *) pending_tracked_send,active_send;
    atomic_uchar channel;atomic_uint channel_generation;bool channel_fixed;
} espnow_session_t;
static unsigned sends,wakes;
static uint16_t esp32_mquickjs_espnow_tx_queue_start_next(int *q) { (void)q;return 0; }
static int64_t esp_timer_get_time(void) { return 42; }
static int esp_now_send(const uint8_t *addr,const uint8_t *data,unsigned len) { (void)addr;(void)data;(void)len;sends++;return 0; }
static int esp32_mquickjs_future_wake(void *r,int t) { (void)r;(void)t;wakes++;return 0; }
'''
def csi_code():
    source=CSI.read_text()
    helper=extract(source,'wifi_csi_channel_admit') if 'static bool wifi_csi_channel_admit(' in source else ''
    return COMMON+CSI_SDK+helper+extract(source,'wifi_csi_rx_callback')
def tx_code():
    source=NOW.read_text()
    helper=extract(source,'espnow_channel_admit') if 'static esp_err_t espnow_channel_admit(' in source else ''
    return COMMON+TX_SDK+helper+extract(source,'espnow_start_tracked_send')+extract(source,'espnow_start_queued_send')

class WirelessChannelEvents(unittest.TestCase):
    def test_csi_conflict_stops_callback_publication(self):
        compile_run(self,csi_code()+r'''
int main(void) {
    wifi_csi_session_t session={.radio_lease={.acquired=true,.generation=17},.lifecycle=WIFI_CSI_RUNNING};
    session.resources=&session.storage;atomic_store(&session.resources->accepting,true);
    uint8_t bytes[4]={0};wifi_csi_info_t info={.buf=bytes,.len=4};
    wifi_csi_rx_callback(&session,&info);
    assert(published==0 && leaves==1);
}
''')

    def test_queued_send_conflict_produces_native_failure_without_tx(self):
        compile_run(self,tx_code()+r'''
int main(void) {
    uint8_t bytes[4]={0};espnow_tx_packet_t packet={.length=4};
    espnow_session_t session={.tx_payloads=bytes,.max_payload_bytes=4,.tx_packets=&packet};
    assert(espnow_start_queued_send(&session));
    assert(sends==0 && atomic_load(&session.queued_send_completed));
    assert(atomic_load(&session.queued_send_error)==ESP_ERR_ESPNOW_CHAN);
}
''')

    def test_csi_following_and_unknown_observation(self):
        compile_run(self,csi_code()+r'''
int main(void) {
    wifi_csi_session_t session={.radio_lease={.acquired=true,.generation=17},.lifecycle=WIFI_CSI_RUNNING};
    session.resources=&session.storage;atomic_store(&session.resources->accepting,true);
    uint8_t bytes[4]={0};wifi_csi_info_t info={.buf=bytes,.len=4};
    conflict=false;fixed=false;
    wifi_csi_rx_callback(&session,&info);assert(published==1 && leaves==1);assert(published_time==UINT64_C(0x300000064) && published_generation==17);
    observation_error=-9;
    wifi_csi_rx_callback(&session,&info);assert(published==1 && leaves==2);
    assert(atomic_load(&session.resources->accepting));
    observation_error=0;wifi_csi_rx_callback(&session,&info);assert(published==2);
}
''')

    def test_csi_fault_latches_without_overwriting_close(self):
        compile_run(self,csi_code()+r'''
int main(void) {
    wifi_csi_session_t session={.radio_lease={.acquired=true,.generation=17},.lifecycle=WIFI_CSI_RUNNING};
    session.resources=&session.storage;atomic_store(&session.resources->accepting,true);
    uint8_t bytes[4]={0};wifi_csi_info_t info={.buf=bytes,.len=4};
    wifi_csi_rx_callback(&session,&info);
    assert(atomic_load(&session.lifecycle)==WIFI_CSI_FAULTED && atomic_load(&session.channel_conflicted));
    conflict=false;wifi_csi_rx_callback(&session,&info);assert(published==0 && leaves==2);
    atomic_store(&session.lifecycle,99);session.resources=&session.storage;atomic_store(&session.resources->accepting,true);
    conflict=true;wifi_csi_rx_callback(&session,&info);
    assert(atomic_load(&session.lifecycle)==99 && published==0 && leaves==3);
}
''')

    def test_tracked_send_conflict_finishes_without_native_callback(self):
        compile_run(self,tx_code()+r'''
int main(void) {
    uint8_t bytes[4]={0};esp32_mquickjs_future_driver_state_t state={.payload=bytes,.payload_length=4};
    espnow_session_t session={.pending_tracked_send=&state};
    assert(espnow_start_tracked_send(&session));
    assert(!sends && wakes==1 && atomic_load(&state.completed));
    assert(state.err==ESP_ERR_ESPNOW_CHAN && !atomic_load(&session.active_send));
    assert(atomic_load(&session.send_failures)==1 && !atomic_load(&session.pending_tracked_send));
}
''')

    def test_following_send_updates_channel_and_keeps_normal_completion(self):
        compile_run(self,tx_code()+r'''
int main(void) {
    conflict=false;fixed=false;
    uint8_t bytes[4]={0};espnow_tx_packet_t packet={.length=4};
    espnow_session_t session={.tx_payloads=bytes,.max_payload_bytes=4,.tx_packets=&packet,.channel=6,.channel_generation=1};
    assert(espnow_start_queued_send(&session));
    assert(sends==1 && !atomic_load(&session.queued_send_completed));
    assert(atomic_load(&session.channel)==11 && atomic_load(&session.channel_generation)==2);
    assert(atomic_load(&session.sent_packets)==1 && !atomic_load(&session.send_failures));
}
''')

    def test_native_observation_failure_retains_raw_tx_error(self):
        compile_run(self,tx_code()+r'''
int main(void) {
    observation_error=-9;conflict=false;
    uint8_t bytes[4]={0};espnow_tx_packet_t packet={.length=4};
    espnow_session_t session={.tx_payloads=bytes,.max_payload_bytes=4,.tx_packets=&packet};
    assert(espnow_start_queued_send(&session));
    assert(!sends && atomic_load(&session.queued_send_completed));
    assert(atomic_load(&session.queued_send_error)==-9);
}
''')

    def test_csi_status_refresh_follows_channel_and_preserves_cleanup_error(self):
        compile_run(self,csi_code()+extract(CSI.read_text(),'wifi_csi_refresh_channel')+r'''
int main(void) {
    wifi_csi_session_t session={.radio_lease={.acquired=true,.generation=17},.lifecycle=WIFI_CSI_RUNNING};
    session.resources=&session.storage;atomic_store(&session.resources->accepting,true);
    fixed=false;conflict=false;
    wifi_csi_refresh_channel(&session);
    assert(session.effective_channel==11 && session.radio_generation==17 && !session.last_error);
    fixed=true;conflict=true;
    wifi_csi_refresh_channel(&session);
    assert(atomic_load(&session.lifecycle)==WIFI_CSI_FAULTED);
    assert(session.last_error==ESP_ERR_INVALID_STATE && !strcmp(session.last_error_code,"WIFI_CSI_CHANNEL_CONFLICT"));
    session.last_error=-9;session.last_error_code="cleanup-error";session.last_error_stage="unregister";
    wifi_csi_refresh_channel(&session);
    assert(session.last_error==-9 && !strcmp(session.last_error_stage,"unregister"));
}
''')

    def test_csi_packet_channel_rejects_before_home_event_arrives(self):
        compile_run(self,csi_code()+r'''
int main(void) {
    wifi_csi_session_t session={.radio_lease={.acquired=true,.generation=17},.lifecycle=WIFI_CSI_RUNNING,.options={.fixed_channel=true,.channel=6}};
    session.resources=&session.storage;atomic_store(&session.resources->accepting,true);conflict=false;
    uint8_t bytes[4]={0};wifi_csi_info_t info={.buf=bytes,.len=4};
    wifi_csi_rx_callback(&session,&info);
    assert(!published && leaves==1 && atomic_load(&session.channel_conflicted));
    assert(atomic_load(&session.lifecycle)==WIFI_CSI_FAULTED);
}
''')
