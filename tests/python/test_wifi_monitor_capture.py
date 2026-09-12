"""Deferred real Monitor capture/resources with explicit Radio/queue boundaries."""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

import test_wifi_rx_target as rx_target
from test_wifi_monitor_resources import production_code


class WiFiMonitorCapture(unittest.TestCase):
    def test_start_failure_drain_retry_fixed_conflict_and_close(self):
        compiler = shutil.which('cc')
        if compiler is None:
            self.skipTest('C compiler unavailable')
        for profile in ['esp32c3/representative', 'esp32s3/representative-psram', 'esp32c5/representative']:
            with self.subTest(profile=profile), tempfile.TemporaryDirectory() as tmp:
                code = production_code(profile)
                code += rx_target.unit(rx_target.COMMON / 'esp32_mquickjs_wifi_rx_filter.c')
                code += BOUNDARY_TYPES
                code += rx_target.unit(rx_target.INTERNAL / 'esp32_mquickjs_wifi_monitor_capture.h')
                code += BOUNDARIES
                code += rx_target.unit(rx_target.ROOT / 'components/esp32_mquickjs/src/modules/wifi_monitor' /
                                       'esp32_mquickjs_wifi_monitor_capture.c')
                path, binary = Path(tmp) / 'fixture.c', Path(tmp) / 'fixture'
                path.write_text(code + MAIN)
                built = subprocess.run([compiler, '-std=c11', '-pthread', '-Wall', '-Wextra', '-Werror',
                                        str(path), '-o', str(binary)], capture_output=True, text=True, timeout=30)
                self.assertEqual(built.returncode, 0, built.stderr)
                result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
                self.assertEqual(result.returncode, 0, result.stderr)


BOUNDARY_TYPES = r'''
typedef int esp_err_t,wifi_mode_t,wifi_second_chan_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_TIMEOUT 3
#define WIFI_MODE_STA 1
#define WIFI_PS_NONE 0
#define WIFI_SECOND_CHAN_NONE 0
#define ESP32_MQUICKJS_WIFI_RADIO_CLIENT_MONITOR 5
typedef struct { uint32_t generation,identity;int client;bool acquired; } esp32_mquickjs_wifi_radio_lease_t;
typedef struct { uint32_t generation,radio_lease_identity,identity;int client;bool acquired,framework_enabled; } esp32_mquickjs_wifi_radio_promiscuous_lease_t;
typedef struct { int unused; } esp32_mquickjs_wifi_promiscuous_subscriber_t;
typedef struct { uint8_t primary;int secondary;uint32_t generation;bool fixed,conflicted; } esp32_mquickjs_wifi_radio_channel_status_t;
typedef struct { uint32_t generation;bool power_save_available;int power_save,cleanup_error;const char *cleanup_stage; } esp32_mquickjs_wifi_radio_status_t;
typedef struct { bool closed,has_event;esp32_mquickjs_wifi_monitor_event_t event; } esp32_mquickjs_event_queue_t;
typedef struct { esp32_mquickjs_wifi_monitor_resources_t *resources;esp32_mquickjs_event_queue_t *queue; } esp32_mquickjs_wifi_monitor_queue_t;
'''

BOUNDARIES = r'''
static esp32_mquickjs_wifi_monitor_capture_t capture;
static esp32_mquickjs_wifi_monitor_resources_t resources;
static esp32_mquickjs_event_queue_t queue;
static esp32_mquickjs_wifi_monitor_queue_t bridge;
static esp32_mquickjs_wifi_rx_target_view_t view;
static unsigned fail_stage,acquires,subscribes,channel_releases,radio_releases,detaches,discard_calls,pool_allocations;
static bool hold_rx,hold_radio,subscribe_retained,channel_conflict,ps_none=true,cancel_start;
static int cleanup_error;
static void (*active_sink)(void *,const esp32_mquickjs_wifi_rx_target_view_t *,esp32_mquickjs_wifi_rx_filter_result_t,uint64_t);
static void *sink_context;
static bool esp32_mquickjs_event_queue_is_closed(const esp32_mquickjs_event_queue_t *q) { return q->closed; }
static size_t esp32_mquickjs_event_queue_discard_all(esp32_mquickjs_event_queue_t *q) {
    assert(!capture.promiscuous.acquired);++discard_calls;
    if(!q->has_event)return 0;
    q->has_event=false;assert(esp32_mquickjs_wifi_monitor_discard_event(&resources,&q->event));return 1;
}
static void esp32_mquickjs_wifi_monitor_queue_detach(esp32_mquickjs_wifi_monitor_queue_t *b) {
    assert(!capture.radio.acquired && !capture.promiscuous.acquired && !capture.channel_claimed);
    ++detaches;b->queue=NULL;queue.closed=true;
}
static int esp32_mquickjs_wifi_radio_acquire(int client,int mode,esp32_mquickjs_wifi_radio_lease_t *lease) {
    assert(!critical_depth && client==ESP32_MQUICKJS_WIFI_RADIO_CLIENT_MONITOR && mode==WIFI_MODE_STA);++acquires;
    if(fail_stage==1)return -11;
    *lease=(esp32_mquickjs_wifi_radio_lease_t){.generation=7,.identity=9,.client=client,.acquired=true};return ESP_OK;
}
static int esp32_mquickjs_wifi_radio_ensure_started(esp32_mquickjs_wifi_radio_lease_t *lease) {
    assert(!critical_depth && lease->acquired);return fail_stage==2?-12:ESP_OK;
}
static int esp32_mquickjs_wifi_radio_get_status(esp32_mquickjs_wifi_radio_status_t *status) {
    assert(!critical_depth);if(fail_stage==3)return -13;
    *status=(esp32_mquickjs_wifi_radio_status_t){.generation=7,.power_save_available=true,.power_save=ps_none?0:1,
        .cleanup_error=cleanup_error,.cleanup_stage=cleanup_error?"promiscuous-restore-enable":NULL};return ESP_OK;
}
static int esp32_mquickjs_wifi_radio_set_channel(esp32_mquickjs_wifi_radio_lease_t *lease,uint8_t primary,int secondary) {
    assert(!critical_depth && lease->acquired && primary==6 && secondary==0);return fail_stage==4?-14:ESP_OK;
}
static int esp32_mquickjs_wifi_radio_get_channel(uint8_t *primary,int *secondary,uint32_t *generation) {
    assert(!critical_depth);if(fail_stage==5)return -15;*primary=6;*secondary=0;*generation=8;return ESP_OK;
}
static int esp32_mquickjs_wifi_radio_lease_channel_status(const esp32_mquickjs_wifi_radio_lease_t *lease,
    esp32_mquickjs_wifi_radio_channel_status_t *status) {
    assert(lease->acquired);
    *status=(esp32_mquickjs_wifi_radio_channel_status_t){.primary=6,.fixed=true,.conflicted=channel_conflict};return ESP_OK;
}
static int esp32_mquickjs_wifi_radio_subscribe_promiscuous(esp32_mquickjs_wifi_radio_lease_t *lease,
    esp32_mquickjs_wifi_promiscuous_subscriber_t *subscriber,const esp32_mquickjs_wifi_rx_filter_t *filter,
    void (*sink)(void *,const esp32_mquickjs_wifi_rx_target_view_t *,esp32_mquickjs_wifi_rx_filter_result_t,uint64_t),void *context,
    esp32_mquickjs_wifi_radio_promiscuous_lease_t *token) {
    assert(!critical_depth && lease->acquired && subscriber && esp32_mquickjs_wifi_rx_filter_valid(filter));++subscribes;
    if(fail_stage==6 && !subscribe_retained)return -16;
    *token=(esp32_mquickjs_wifi_radio_promiscuous_lease_t){.generation=7,.identity=subscribes,.radio_lease_identity=9,.acquired=true};
    active_sink=sink;sink_context=context;
    if(fail_stage==6)return -16;
    if(cancel_start)esp32_mquickjs_wifi_monitor_capture_request_stop(context,true);
    sink(context,&view,ESP32_MQUICKJS_WIFI_RX_FILTER_ACCEPT,10);
    return ESP_OK;
}
static void esp32_mquickjs_wifi_radio_release_promiscuous(esp32_mquickjs_wifi_radio_promiscuous_lease_t *token) {
    assert(!critical_depth && token->acquired);
    if(hold_rx)return;
    memset(token,0,sizeof(*token));active_sink=NULL;sink_context=NULL;
}
static void esp32_mquickjs_wifi_radio_release_channel(esp32_mquickjs_wifi_radio_lease_t *lease) {
    assert(!critical_depth && lease->acquired && !capture.promiscuous.acquired);++channel_releases;
}
static void esp32_mquickjs_wifi_radio_release(esp32_mquickjs_wifi_radio_lease_t *lease) {
    assert(!critical_depth && lease->acquired && !capture.promiscuous.acquired && !capture.channel_claimed);
    ++radio_releases;if(!hold_radio)memset(lease,0,sizeof(*lease));
}
static bool publish(const esp32_mquickjs_wifi_monitor_event_t *event,void *opaque) {
    (void)opaque;assert(!critical_depth);
    if(queue.closed || queue.has_event)return false;
    queue.event=*event;queue.has_event=true;return true;
}
static void *allocate_zero(size_t count,size_t size,void *opaque) { (void)opaque;++pool_allocations;return calloc(count,size); }
static void *allocate(size_t size,void *opaque) { return allocate_zero(1,size,opaque); }
static void free_pool(void *p,void *opaque) { (void)opaque;assert(pool_allocations);--pool_allocations;free(p); }
'''

MAIN = r'''
static void setup(bool fixed) {
    assert(!pool_allocations);
    memset(&capture,0,sizeof(capture));memset(&resources,0,sizeof(resources));memset(&queue,0,sizeof(queue));
    bridge=(esp32_mquickjs_wifi_monitor_queue_t){&resources,&queue};
    fail_stage=0;hold_rx=hold_radio=subscribe_retained=channel_conflict=cancel_start=false;ps_none=true;cleanup_error=0;
    acquires=subscribes=channel_releases=radio_releases=detaches=discard_calls=0;
    esp32_mquickjs_wifi_monitor_allocator_t allocator={allocate_zero,allocate,free_pool,NULL};
    assert(esp32_mquickjs_wifi_monitor_resources_init(&resources,1,2,24,false,&allocator,publish,NULL));
    esp32_mquickjs_wifi_monitor_capture_options_t options={.filter={.type_mask=4,.sample_every=1,.valid_only=true},.channel=fixed?6:0};
    assert(esp32_mquickjs_wifi_monitor_capture_init(&capture,&resources,&bridge,&options)==ESP_OK);
    assert(esp32_mquickjs_wifi_monitor_capture_init(&capture,&resources,&bridge,&options)==ESP_ERR_INVALID_STATE);
}
static void cleanup(void) {
    fail_stage=0;hold_rx=hold_radio=false;cleanup_error=0;
    assert(esp32_mquickjs_wifi_monitor_capture_close(&capture)==ESP_OK);
    assert(capture.state==ESP32_MQUICKJS_WIFI_MONITOR_CLOSED && !capture.radio.acquired && !capture.promiscuous.acquired);
    assert(!bridge.queue && !queue.has_event && !active_sink);
    assert(esp32_mquickjs_wifi_monitor_resources_deinit(&resources));
    assert(!pthread_mutex_destroy(&resources.lock));
}
static void failures(void) {
    for(unsigned stage=1;stage<=6;stage++) {
        setup(true);fail_stage=stage;
        assert(esp32_mquickjs_wifi_monitor_capture_start(&capture)==-(int)(10+stage));
        assert(capture.last_error==-(int)(10+stage) && capture.last_stage);
        assert(capture.state==ESP32_MQUICKJS_WIFI_MONITOR_STOPPED && !capture.promiscuous.acquired && !capture.channel_claimed);
        assert(subscribes==(stage==6?1U:0U));cleanup();
    }
    setup(false);ps_none=false;capture.options.require_power_save_none=true;
    assert(esp32_mquickjs_wifi_monitor_capture_start(&capture)==ESP_ERR_INVALID_STATE);
    assert(!strcmp(capture.last_stage,"power-save-policy") && !subscribes);cleanup();
    setup(true);fail_stage=6;subscribe_retained=hold_rx=true;cleanup_error=-88;
    assert(esp32_mquickjs_wifi_monitor_capture_start(&capture)==-16);
    assert(capture.last_error==-16 && capture.cleanup_error==-88 && capture.promiscuous.acquired && capture.channel_claimed);
    assert(!channel_releases && !discard_calls && !radio_releases);
    assert(esp32_mquickjs_wifi_monitor_capture_start(&capture)==ESP_ERR_INVALID_STATE);
    assert(esp32_mquickjs_wifi_monitor_capture_close(&capture)==-88 && !detaches);
    cleanup();assert(channel_releases==1 && radio_releases==1 && detaches==1);
}
static void drain_and_retained_frame(void) {
    setup(true);assert(esp32_mquickjs_wifi_monitor_capture_start(&capture)==ESP_OK && queue.has_event);
    assert(capture.radio_generation==7 && capture.channel_generation==8 && capture.effective_channel==6);
    assert(esp32_mquickjs_wifi_monitor_capture_start(&capture)==ESP_OK && acquires==1 && subscribes==1);
    esp32_mquickjs_wifi_monitor_event_t event=queue.event;queue.has_event=false;
    assert(esp32_mquickjs_wifi_monitor_take_event(&resources,&event));
    esp32_mquickjs_wifi_monitor_ref_t ref={0};assert(esp32_mquickjs_wifi_monitor_retain_frame(&resources,&event,&ref));
    hold_rx=true;
    assert(esp32_mquickjs_wifi_monitor_capture_stop(&capture)==ESP_ERR_TIMEOUT);
    assert(capture.state==ESP32_MQUICKJS_WIFI_MONITOR_STOPPING && !capture.cleanup_error && !capture.cleanup_stage);
    active_sink(sink_context,&view,ESP32_MQUICKJS_WIFI_RX_FILTER_ACCEPT,11);
    assert(!queue.has_event && capture.channel_claimed && !channel_releases);
    hold_rx=false;assert(esp32_mquickjs_wifi_monitor_capture_stop(&capture)==ESP_OK && capture.radio.acquired);
    assert(esp32_mquickjs_wifi_monitor_capture_start(&capture)==ESP_OK && subscribes==2 && acquires==1);
    hold_radio=true;
    assert(esp32_mquickjs_wifi_monitor_capture_close(&capture)==ESP_ERR_TIMEOUT && bridge.queue && !detaches);
    hold_radio=false;assert(esp32_mquickjs_wifi_monitor_capture_close(&capture)==ESP_OK);
    assert(esp32_mquickjs_wifi_monitor_capture_start(&capture)==ESP_ERR_INVALID_STATE);
    assert(!esp32_mquickjs_wifi_monitor_resources_deinit(&resources));
    assert(esp32_mquickjs_wifi_monitor_close_frame(&resources,&event));
    const uint8_t *data;size_t length;
    assert(esp32_mquickjs_wifi_monitor_ref_data(&ref,&data,&length) && length==24 && data[0]==8);
    assert(esp32_mquickjs_wifi_monitor_release_ref(&ref));cleanup();
}
static void callback_stop_and_cancel(void) {
    setup(true);assert(esp32_mquickjs_wifi_monitor_capture_start(&capture)==ESP_OK);
    channel_conflict=true;active_sink(sink_context,&view,ESP32_MQUICKJS_WIFI_RX_FILTER_ACCEPT,12);
    assert(atomic_load(&capture.channel_conflicted) && atomic_load(&capture.stop_requested));
    assert(esp32_mquickjs_wifi_monitor_capture_start(&capture)==ESP_ERR_INVALID_STATE);
    cleanup();
    setup(false);cancel_start=true;
    assert(esp32_mquickjs_wifi_monitor_capture_start(&capture)==ESP_ERR_INVALID_STATE);
    assert(!strcmp(capture.last_stage,"start-cancelled") && !queue.has_event);
    assert(atomic_load(&capture.close_requested));cleanup();
}
int main(void) {
    wifi_pkt_rx_ctrl_t rx={0};rx.sig_len=24;rx.channel=6;
#if CONFIG_SOC_WIFI_HE_SUPPORT
    rx.dump_len=24;
#endif
    uint8_t packet[sizeof(rx)+24];memcpy(packet,&rx,sizeof(rx));memset(packet+sizeof(rx),0,24);packet[sizeof(rx)]=8;
    assert(esp32_mquickjs_wifi_rx_target_view(packet,WIFI_PKT_DATA,&view)==ESP32_MQUICKJS_WIFI_RX_SPAN_PACKET);
    failures();drain_and_retained_frame();callback_stop_and_cancel();
    assert(!critical_depth && !pool_allocations);
}
'''
