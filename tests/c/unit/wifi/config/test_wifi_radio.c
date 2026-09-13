/* Compile the production Radio, replacing only SDK/RTOS calls. Lifecycle
 * resets go through production stop/shutdown, never a separate test model. */
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../../../../components/esp32_mquickjs/src/modules/wifi_radio/esp32_mquickjs_wifi_wait.c"
#include "../../../../../components/esp32_mquickjs/src/modules/wifi_common/esp32_mquickjs_wifi_rx.c"
#include "../../../../../components/esp32_mquickjs/src/modules/wifi_common/esp32_mquickjs_wifi_rx_filter.c"
#include "../../../../../components/esp32_mquickjs/src/modules/wifi_common/esp32_mquickjs_wifi_promiscuous_broker.c"
#include "../../../../../components/esp32_mquickjs/src/modules/wifi_common/esp32_mquickjs_wifi_promiscuous_driver.c"
#include "../../../../../components/esp32_mquickjs/src/modules/wifi_radio/esp32_mquickjs_wifi_interval.c"
#include "../../../../../components/esp32_mquickjs/src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c"

_Thread_local int test_critical_depth;
/* Radio fixture boundary only; complete quota/allocator-return tests link the
 * production manager in test_memory_wireless and the storage budget fixtures. */
void *esp32_mquickjs_memory_wireless_alloc(const char *owner, size_t size,
    esp32_mquickjs_memory_class_t policy, esp32_mquickjs_memory_budget_role_t role)
{
    (void)policy;
    assert(owner && *owner && !test_critical_depth);
    assert(role > ESP32_MQUICKJS_MEMORY_BUDGET_NONE && role < ESP32_MQUICKJS_MEMORY_BUDGET_ROLE_COUNT);
    return malloc(size ? size : 1);
}
void *esp32_mquickjs_memory_wireless_calloc(const char *owner, size_t count,
    size_t size, esp32_mquickjs_memory_class_t policy, esp32_mquickjs_memory_budget_role_t role)
{
    if (size && count > SIZE_MAX / size) return NULL;
    void *p = esp32_mquickjs_memory_wireless_alloc(owner, count * size, policy, role);
    if (p) memset(p, 0, count * size);
    return p;
}
void esp32_mquickjs_memory_payload_free(void *p)
{
    assert(!test_critical_depth);
    free(p);
}
static int fail_at, calls[21];
static bool mode_error_after_start;
static bool ap_mode_suppress_stop,ap_mode_partial_error,ap_mode_readback_error,ap_mode_changed;
static int loop_error,handler_error,handler_registrations,unregister_error,unregister_calls;
static void unregister_signal(void);
esp_err_t esp_event_handler_instance_unregister(esp_event_base_t base,int32_t id,esp_event_handler_instance_t instance) { assert(base==WIFI_EVENT && id==ESP_EVENT_ANY_ID && instance && !test_critical_depth);unregister_calls++;unregister_signal();return unregister_error; }
static void (*channel_handler)(void *,esp_event_base_t,int32_t,void *);
static void (*lifecycle_handler)(void *,esp_event_base_t,int32_t,void *);
static void (*on_event_delay)(void);
static bool suppress_start_events,suppress_stop_events,defer_fence;
static int fence_error,fence_posts,lifecycle_handler_error;
static wifi_radio_event_fence_t queued_fence;
void radio_test_delay(void) { if(on_event_delay)on_event_delay(); }
esp_err_t esp_event_post(esp_event_base_t base,int32_t id,const void *data,size_t size,unsigned ticks) {
    assert(base==ESP32QJS_WIFI_RADIO_CONTROL_EVENT && id==1 && size==sizeof(queued_fence) && !ticks && !test_critical_depth);
    fence_posts++;if(fence_error)return fence_error;queued_fence=*(const wifi_radio_event_fence_t *)data;
    if(!defer_fence)lifecycle_handler(NULL,base,id,&queued_fence);
    return 0;
}
static void driver_lifecycle_events(wifi_mode_t mode,bool start) {
    if(mode & WIFI_MODE_STA)channel_handler(NULL,WIFI_EVENT,start?WIFI_EVENT_STA_START:WIFI_EVENT_STA_STOP,NULL);
    if(mode & WIFI_MODE_AP)channel_handler(NULL,WIFI_EVENT,start?WIFI_EVENT_AP_START:WIFI_EVENT_AP_STOP,NULL);
}

esp_err_t esp_event_loop_create_default(void) { assert(!test_critical_depth);return loop_error; }
esp_err_t esp_event_handler_instance_register(esp_event_base_t base,int32_t id,void (*handler)(void *,esp_event_base_t,int32_t,void *),void *arg,esp_event_handler_instance_t *instance) {
    (void)arg;assert(!test_critical_depth);
    if(base==ESP32QJS_WIFI_RADIO_CONTROL_EVENT) {
        assert(id==ESP_EVENT_ANY_ID);if(lifecycle_handler_error)return lifecycle_handler_error;lifecycle_handler=handler;*instance=(void *)2;return 0;
    }
    assert(base==WIFI_EVENT && id==ESP_EVENT_ANY_ID);
    handler_registrations++;if(handler_error)return handler_error;channel_handler=handler;*instance=(void *)1;return 0;
}
static wifi_mode_t driver_mode;
static uint8_t driver_channel = 1;
static wifi_second_chan_t driver_secondary;
static bool station_connected;
static int channel_read_error;
static _Thread_local bool pause_channel_read;
static bool driver_promiscuous;
static uint32_t driver_packet_filter = 7, driver_control_filter;
int64_t esp_timer_get_time(void) { return 100; }
static bool rx_test_enabled, rx_synchronous;
static wifi_promiscuous_cb_t driver_rx_callback;
static uint8_t rx_packet[24] = { 0x08 };
static esp_err_t driver_call(int stage);
static void rx_after_driver_mutation(void) {
    if(rx_synchronous && driver_rx_callback) driver_rx_callback(rx_packet,WIFI_PKT_DATA);
}
/* Radio/registry integration fixture replaces only the SDK callback span
 * adapter boundary. Actual legacy/HE layout and parser paths have separate
 * recorded-SDK fixtures; this unit uses the production MAC parser/filter. */
esp32_mquickjs_wifi_rx_span_status_t esp32_mquickjs_wifi_rx_target_view(
    const void *buffer,wifi_promiscuous_pkt_type_t type,esp32_mquickjs_wifi_rx_target_view_t *output)
{
    memset(output,0,sizeof(*output));
    if(!buffer)return output->status=ESP32_MQUICKJS_WIFI_RX_SPAN_INVALID_ARGUMENT;
    output->status=ESP32_MQUICKJS_WIFI_RX_SPAN_PACKET;
    output->metadata.available=true;output->metadata.type=(esp32_mquickjs_wifi_packet_type_t)type;
    output->metadata.rssi=-50;output->bytes=buffer;output->readable_length=24;
    (void)esp32_mquickjs_wifi_rx_parse_header(buffer,24,&output->header);
    output->header_type_matches=output->header.type==output->metadata.type && output->header.version==0;
    return output->status;
}
esp_err_t esp_wifi_set_promiscuous_rx_cb(wifi_promiscuous_cb_t callback) {
    assert(rx_test_enabled);int err=driver_call(19);
    if(!err)driver_rx_callback=callback;rx_after_driver_mutation();return err;
}
esp_err_t esp_wifi_get_promiscuous_filter(wifi_promiscuous_filter_t *filter) { assert(!test_critical_depth);filter->filter_mask=driver_packet_filter; return ESP_OK; }
esp_err_t esp_wifi_get_promiscuous_ctrl_filter(wifi_promiscuous_filter_t *filter) { assert(!test_critical_depth);filter->filter_mask=driver_control_filter; return ESP_OK; }
esp_err_t esp_wifi_set_promiscuous_filter(const wifi_promiscuous_filter_t *filter) {
    int err=driver_call(17);if(!err)driver_packet_filter=filter->filter_mask;rx_after_driver_mutation();return err;
}
esp_err_t esp_wifi_set_promiscuous_ctrl_filter(const wifi_promiscuous_filter_t *filter) {
    int err=driver_call(18);if(!err)driver_control_filter=filter->filter_mask;rx_after_driver_mutation();return err;
}
static wifi_ps_type_t driver_ps;
static int8_t driver_power = 80;
static bool activation_test;
static int activation_case,activation_calls;
static pthread_mutex_t schedule_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t schedule_cond = PTHREAD_COND_INITIALIZER;
static int pause_at, paused, resume_driver, mutex_contended;
static atomic_bool release_entered, release_finished;
static bool unregistered;
static void unregister_signal(void) {
    pthread_mutex_lock(&schedule_lock);unregistered=true;
    pthread_cond_broadcast(&schedule_cond);pthread_mutex_unlock(&schedule_lock);
}

void radio_test_mutex_contended(void)
{
    pthread_mutex_lock(&schedule_lock);
    mutex_contended = 1;
    pthread_cond_broadcast(&schedule_cond);
    pthread_mutex_unlock(&schedule_lock);
}

static esp_err_t driver_call(int stage)
{
    assert(test_critical_depth == 0);
    calls[stage]++;
    pthread_mutex_lock(&schedule_lock);
    if (stage == pause_at) {
        paused = 1;
        pthread_cond_broadcast(&schedule_cond);
        while (!resume_driver) pthread_cond_wait(&schedule_cond, &schedule_lock);
    }
    pthread_mutex_unlock(&schedule_lock);
    return stage == fail_at ? 0x700 + stage : ESP_OK;
}
esp_err_t esp32_mquickjs_nvs_flash_ensure_initialized(void) { return driver_call(1); }
esp_err_t esp_wifi_init(const wifi_init_config_t *c) { (void)c; return driver_call(2); }
esp_err_t esp_wifi_connectionless_module_set_wake_interval(uint16_t interval) { (void)interval;return driver_call(20); }
esp_err_t esp_wifi_set_storage(int s) { (void)s; return driver_call(3); }
esp_err_t esp_wifi_get_mode(wifi_mode_t *m) { *m = driver_mode; if(ap_mode_readback_error && ap_mode_changed)return -99; return mode_error_after_start && s_radio.started ? -88 : driver_call(4); }
esp_err_t esp_wifi_set_mode(wifi_mode_t m) {
    int e=driver_call(5);wifi_mode_t old=driver_mode;
    if(!e || ap_mode_partial_error){
        driver_mode=m;
        if(s_radio.started && old==WIFI_MODE_APSTA && m==WIFI_MODE_STA)ap_mode_changed=true;
        if(s_radio.started && !suppress_start_events)driver_lifecycle_events((wifi_mode_t)(m & ~old),true);
        if(s_radio.started && !ap_mode_suppress_stop)driver_lifecycle_events((wifi_mode_t)(old & ~m),false);
    }
    return ap_mode_partial_error ? -88 : e;
}
esp_err_t esp_wifi_start(void) { int err=driver_call(6);if(!err && !suppress_start_events)driver_lifecycle_events(driver_mode,true);return err; }
esp_err_t esp_wifi_stop(void) { int err=driver_call(9);if(!err && !suppress_stop_events)driver_lifecycle_events(driver_mode,false);return err; }
esp_err_t esp_wifi_deinit(void) { return driver_call(10); }
esp_err_t esp_wifi_get_country(wifi_country_t *c) { c->schan = 1; c->nchan = 11; return ESP_OK; }
esp_err_t esp_wifi_get_channel(uint8_t *p, wifi_second_chan_t *s) { assert(!test_critical_depth);*p = driver_channel; *s = driver_secondary; if(pause_channel_read) (void)driver_call(15);return channel_read_error; }
esp_err_t esp_wifi_set_channel(uint8_t p, wifi_second_chan_t s) { int e = driver_call(7); if (!e) {driver_channel = p;driver_secondary = s;} return e; }
static int activation_boundary(void) {
    if(!activation_test)return 0;
    assert(s_radio.started && s_radio.lifecycle.identity && !test_critical_depth);
    activation_calls++;
    if((activation_case==1 && activation_calls==1) ||
       (activation_case==2 && activation_calls==2) ||
       (activation_case==3 && activation_calls==3))return -77;
    if((activation_case==5 && activation_calls==4) ||
       (activation_case==6 && activation_calls==5))return -88;
    return 0;
}
esp_err_t esp_wifi_get_max_tx_power(int8_t *p) {
    *p=driver_power;int e=driver_call(14);if(e)return e;e=activation_boundary();
    if(activation_test && activation_calls==3 && activation_case>=4 && activation_case<=7)*p=84;
    if(activation_test && activation_calls==5 && activation_case==7)*p=68;
    if(activation_test && activation_calls==1 && activation_case==8)*p=84;
    return e;
}
esp_err_t esp_wifi_set_max_tx_power(int8_t p) {
    int e=driver_call(13);if(e)return e;
    /* Model a partial native write even when the injected setter fails. */
    driver_power=p>78?78:p;return activation_boundary();
}
esp_err_t esp_wifi_get_ps(wifi_ps_type_t *p) { *p = driver_ps; return driver_call(12); }
esp_err_t esp_wifi_set_ps(wifi_ps_type_t p) { int e = driver_call(11); if (!e) driver_ps = p; return e; }
esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t *a) { a->primary = driver_channel; return station_connected ? ESP_OK : ESP_ERR_WIFI_NOT_CONNECT; }
esp_err_t esp_wifi_get_config(int i, wifi_config_t *c) { (void)i; c->ap.channel = 0; return ESP_OK; }
esp_err_t esp_wifi_get_promiscuous(bool *e) { *e = driver_promiscuous; return ESP_OK; }
esp_err_t esp_wifi_set_promiscuous(bool e) { int r = driver_call(8); if (!r) driver_promiscuous = e; rx_after_driver_mutation(); return r; }

esp_err_t esp_wifi_set_config(int iface,wifi_config_t *config) {
    assert(iface == WIFI_IF_AP && driver_mode == WIFI_MODE_AP && !s_radio.started);
    int err = driver_call(16);if (!err) driver_channel = config->ap.channel;return err;
}

static esp32_mquickjs_wifi_radio_lease_t acquire(void)
{
    esp32_mquickjs_wifi_radio_lease_t lease;
    assert(esp32_mquickjs_wifi_radio_acquire(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_ESPNOW, WIFI_MODE_STA, &lease) == ESP_OK);
    return lease;
}
static void test_stale(void)
{
    esp32_mquickjs_wifi_radio_lease_t a = acquire(), old = a, b = acquire();
    esp32_mquickjs_wifi_radio_status_t status;
    esp32_mquickjs_wifi_radio_release(&a);
    esp32_mquickjs_wifi_radio_release(&a);
    esp32_mquickjs_wifi_radio_release(&old);
    assert(esp32_mquickjs_wifi_radio_get_status(&status) == ESP_OK);
    assert(status.clients[ESP32_MQUICKJS_WIFI_RADIO_CLIENT_ESPNOW] == 1);
    assert(esp32_mquickjs_wifi_radio_ensure_started(&old) == ESP_ERR_INVALID_STATE);
    assert(esp32_mquickjs_wifi_radio_set_channel(&old, 6, WIFI_SECOND_CHAN_NONE) != ESP_OK);
    assert(esp32_mquickjs_wifi_radio_ensure_started(&b) == ESP_OK);
    esp32_mquickjs_wifi_radio_release(&b);
}
static void test_failure(int stage)
{
    esp32_mquickjs_wifi_radio_lease_t a = acquire();
    fail_at = stage;
    assert(esp32_mquickjs_wifi_radio_ensure_started(&a) == 0x700 + stage);
    int before[21]; memcpy(before, calls, sizeof(calls));
    assert(esp32_mquickjs_wifi_radio_ensure_started(&a) == 0x700 + stage);
    assert(memcmp(before, calls, sizeof(calls)) == 0);
#ifdef RADIO_DIAGNOSTICS_TEST
    esp32_mquickjs_wifi_radio_status_t status;
    assert(esp32_mquickjs_wifi_radio_get_status(&status) == ESP_OK);
    assert(status.restart_required == (stage <= 2));
    assert(status.fault_error == 0x700 + stage);
    assert(status.driver_owned == (stage > 2));
    assert(!status.started);
#endif
}
static void test_channel(void)
{
    esp32_mquickjs_wifi_radio_lease_t a = acquire(), b = acquire();
    assert(esp32_mquickjs_wifi_radio_ensure_started(&a) == ESP_OK);
    assert(esp32_mquickjs_wifi_radio_set_channel(&a, 6, WIFI_SECOND_CHAN_NONE) == ESP_OK);
    assert(esp32_mquickjs_wifi_radio_set_channel(&a, 6, WIFI_SECOND_CHAN_NONE) == ESP_OK);
    assert(calls[7] == 1);
    assert(esp32_mquickjs_wifi_radio_set_channel(&b, 6, WIFI_SECOND_CHAN_NONE) == ESP_OK);
    assert(calls[7] == 1);
    assert(esp32_mquickjs_wifi_radio_set_channel(&b, 11, WIFI_SECOND_CHAN_NONE) == ESP_ERR_INVALID_STATE);
    esp32_mquickjs_wifi_radio_release_channel(&b);
    fail_at = 7;
    assert(esp32_mquickjs_wifi_radio_set_channel(&a, 11, WIFI_SECOND_CHAN_NONE) == 0x707);
    assert(s_radio.leases[0].primary_channel == 6 && driver_channel == 6);
}
static void *start_thread(void *p) { assert(esp32_mquickjs_wifi_radio_ensure_started(p) == ESP_OK); return NULL; }
static void *release_thread(void *p) {
    atomic_store(&release_entered, true);
    esp32_mquickjs_wifi_radio_release(p);
    pthread_mutex_lock(&schedule_lock);
    atomic_store(&release_finished, true);
    pthread_cond_broadcast(&schedule_cond);
    pthread_mutex_unlock(&schedule_lock);
    return NULL;
}
static void test_concurrent(bool release)
{
    esp32_mquickjs_wifi_radio_lease_t a = acquire(), b = acquire(), copy = a;
    pthread_t first, second;
    pause_at = 2;
    pthread_create(&first, NULL, start_thread, &a);
    pthread_mutex_lock(&schedule_lock);
    while (!paused) pthread_cond_wait(&schedule_cond, &schedule_lock);
    pthread_mutex_unlock(&schedule_lock);
    esp32_mquickjs_wifi_radio_status_t status;
    assert(esp32_mquickjs_wifi_radio_get_status(&status) == ESP_OK);
    assert(status.driver_state == ESP32_MQUICKJS_WIFI_RADIO_INITIALIZING);
    pthread_create(&second, NULL, release ? release_thread : start_thread, release ? &copy : &b);
    if (release) {
        pthread_mutex_lock(&schedule_lock);
        while (!mutex_contended && !atomic_load(&release_finished))
            pthread_cond_wait(&schedule_cond, &schedule_lock);
        assert(!atomic_load(&release_finished));
        pthread_mutex_unlock(&schedule_lock);
    }
    pthread_mutex_lock(&schedule_lock);
    resume_driver = 1; pthread_cond_broadcast(&schedule_cond);
    pthread_mutex_unlock(&schedule_lock);
    pthread_join(first, NULL); pthread_join(second, NULL);
    assert(calls[1] == 1 && calls[2] == 1 && calls[6] == 1);
}
static void test_exhaustion(void)
{
    s_radio.next_lease_identity = UINT32_MAX;
    esp32_mquickjs_wifi_radio_lease_t a = acquire(), b;
    assert(a.identity == UINT32_MAX);
    assert(esp32_mquickjs_wifi_radio_acquire(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_ESPNOW, WIFI_MODE_STA, &b) != ESP_OK);
    esp32_mquickjs_wifi_radio_release(&a);
    assert(esp32_mquickjs_wifi_radio_acquire(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_ESPNOW, WIFI_MODE_STA, &b) != ESP_OK);
}
/* New regression scenarios are source-only until the Wi-Fi API test phase. */
static void test_promiscuous_shared_identity(void)
{
    esp32_mquickjs_wifi_radio_lease_t a = acquire(), b = acquire();
    esp32_mquickjs_wifi_radio_promiscuous_lease_t first, second, reopened;
    assert(esp32_mquickjs_wifi_radio_ensure_started(&a) == ESP_OK);
    assert(esp32_mquickjs_wifi_radio_acquire_promiscuous(&a, &first) == ESP_OK);
    int enables = calls[8];
    assert(esp32_mquickjs_wifi_radio_acquire_promiscuous(&b, &second) == ESP_OK);
    assert(calls[8] == enables && driver_promiscuous);
    esp32_mquickjs_wifi_radio_promiscuous_lease_t old = first;
    esp32_mquickjs_wifi_radio_release_promiscuous(&first);
    assert(!first.acquired && second.acquired && driver_promiscuous && calls[8] == enables);
    assert(esp32_mquickjs_wifi_radio_acquire_promiscuous(&a, &reopened) == ESP_OK);
    assert(reopened.identity != old.identity);
    esp32_mquickjs_wifi_radio_release_promiscuous(&old);
    assert(reopened.acquired && driver_promiscuous && calls[8] == enables);
    esp32_mquickjs_wifi_radio_release(&b);
    assert(!b.acquired && driver_promiscuous && calls[8] == enables);
    esp32_mquickjs_wifi_radio_release_promiscuous(&second);
    assert(driver_promiscuous && calls[8] == enables);
    esp32_mquickjs_wifi_radio_release_promiscuous(&reopened);
    assert(!reopened.acquired && !driver_promiscuous && calls[8] == enables + 1);
    esp32_mquickjs_wifi_radio_release(&a);
}

static void test_promiscuous_cleanup(void)
{
    esp32_mquickjs_wifi_radio_lease_t a = acquire();
    esp32_mquickjs_wifi_radio_promiscuous_lease_t prom;
    assert(esp32_mquickjs_wifi_radio_ensure_started(&a) == ESP_OK);
    assert(esp32_mquickjs_wifi_radio_acquire_promiscuous(&a, &prom) == ESP_OK);
    esp32_mquickjs_wifi_radio_release_promiscuous(&prom);
    esp32_mquickjs_wifi_radio_status_t status;
    assert(esp32_mquickjs_wifi_radio_get_status(&status) == ESP_OK);
    assert(status.driver_state == ESP32_MQUICKJS_WIFI_RADIO_STARTED);
    assert(status.cleanup_stage == NULL && status.fault_stage == NULL);
    assert(esp32_mquickjs_wifi_radio_acquire_promiscuous(&a, &prom) == ESP_OK);
    fail_at = 8;
    esp32_mquickjs_wifi_radio_release_promiscuous(&prom);
    assert(prom.acquired && s_radio.promiscuous_claimed && driver_promiscuous);
    esp32_mquickjs_wifi_radio_release(&a);
    assert(a.acquired && wifi_radio_lease_valid(&a));
    fail_at = 0;
    esp32_mquickjs_wifi_radio_release_promiscuous(&prom);
    assert(!prom.acquired && !s_radio.promiscuous_claimed && !driver_promiscuous);
    assert(esp32_mquickjs_wifi_radio_get_status(&status) == ESP_OK);
    assert(status.driver_state == ESP32_MQUICKJS_WIFI_RADIO_STARTED);
    assert(status.cleanup_stage == NULL && status.fault_stage == NULL);
    esp32_mquickjs_wifi_radio_release(&a);
    assert(!a.acquired);
}

static void test_promiscuous_failed_acquire(void)
{
    esp32_mquickjs_wifi_radio_lease_t a = acquire();
    esp32_mquickjs_wifi_radio_promiscuous_lease_t prom;
    assert(esp32_mquickjs_wifi_radio_ensure_started(&a) == ESP_OK);
    fail_at = 8;
    assert(esp32_mquickjs_wifi_radio_acquire_promiscuous(&a, &prom) == 0x708);
    assert(prom.acquired && s_radio.promiscuous_claimed);
    esp32_mquickjs_wifi_radio_status_t status;
    assert(esp32_mquickjs_wifi_radio_get_status(&status) == ESP_OK);
    assert(status.promiscuous_owners == 1 && status.cleanup_stage);
    assert(!strcmp(status.fault_stage, "promiscuous-enable"));
    assert(!strcmp(status.cleanup_stage, "promiscuous-restore-enable"));
    esp32_mquickjs_wifi_radio_release(&a);assert(a.acquired);
    fail_at = 0;
    esp32_mquickjs_wifi_radio_release_promiscuous(&prom);
    assert(!prom.acquired && !s_radio.promiscuous_claimed && !driver_promiscuous);
    esp32_mquickjs_wifi_radio_release(&a);assert(!a.acquired);
}

static void test_promiscuous_identity_exhausted(void)
{
    esp32_mquickjs_wifi_radio_lease_t a = acquire();
    esp32_mquickjs_wifi_radio_promiscuous_lease_t prom;
    assert(esp32_mquickjs_wifi_radio_ensure_started(&a) == ESP_OK);
    s_radio.next_promiscuous_identity = UINT32_MAX;
    assert(esp32_mquickjs_wifi_radio_acquire_promiscuous(&a, &prom) == ESP_OK);
    assert(prom.identity == UINT32_MAX);
    esp32_mquickjs_wifi_radio_release_promiscuous(&prom);
    int before = calls[8];
    assert(esp32_mquickjs_wifi_radio_acquire_promiscuous(&a, &prom) == ESP_ERR_INVALID_STATE);
    assert(!prom.acquired && calls[8] == before);
    esp32_mquickjs_wifi_radio_status_t status;
    assert(esp32_mquickjs_wifi_radio_get_status(&status) == ESP_OK);
    assert(status.promiscuous_identity_exhausted && !status.promiscuous_owners);
    esp32_mquickjs_wifi_radio_release(&a);
}

typedef struct { unsigned calls, accepted; bool block; } radio_rx_context_t;
static pthread_mutex_t rx_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t rx_changed = PTHREAD_COND_INITIALIZER;
static bool rx_paused, rx_proceed;
static wifi_promiscuous_cb_t rx_worker_callback;
static void radio_rx_sink(void *opaque, const esp32_mquickjs_wifi_rx_target_view_t *view,
    esp32_mquickjs_wifi_rx_filter_result_t result, uint64_t now)
{
    radio_rx_context_t *context = opaque;
    assert(!test_critical_depth && now == 100);
    ++context->calls;
    if (result == ESP32_MQUICKJS_WIFI_RX_FILTER_ACCEPT) {
        assert(view->bytes && view->readable_length == sizeof(rx_packet));
        ++context->accepted;
    }
    if (context->block) {
        pthread_mutex_lock(&rx_lock); rx_paused = true; pthread_cond_broadcast(&rx_changed);
        while (!rx_proceed) pthread_cond_wait(&rx_changed, &rx_lock);
        pthread_mutex_unlock(&rx_lock);
    }
}
static void *radio_rx_worker(void *unused)
{
    (void)unused; rx_worker_callback(rx_packet, WIFI_PKT_DATA); return NULL;
}
static esp_err_t radio_rx_subscribe(esp32_mquickjs_wifi_radio_lease_t *radio,
    esp32_mquickjs_wifi_promiscuous_subscriber_t *subscriber, radio_rx_context_t *context,
    const esp32_mquickjs_wifi_rx_filter_t *filter, esp32_mquickjs_wifi_radio_promiscuous_lease_t *token)
{
    return esp32_mquickjs_wifi_radio_subscribe_promiscuous(radio, subscriber, filter, radio_rx_sink, context, token);
}

static void test_promiscuous_rx_union(void)
{
    rx_test_enabled = rx_synchronous = true;
    esp32_mquickjs_wifi_radio_lease_t a = acquire(), b = acquire(), csi = acquire();
    assert(esp32_mquickjs_wifi_radio_ensure_started(&a) == ESP_OK);
    esp32_mquickjs_wifi_radio_promiscuous_lease_t first, second, enable;
    esp32_mquickjs_wifi_promiscuous_subscriber_t subscribers[2];
    radio_rx_context_t contexts[2] = {0};
    esp32_mquickjs_wifi_rx_filter_t filter = { .type_mask = 4, .sample_every = 1, .valid_only = true };
    assert(radio_rx_subscribe(&a, &subscribers[0], &contexts[0], &filter, &first) == ESP_OK);
    assert(!contexts[0].calls && driver_rx_callback && driver_promiscuous && driver_packet_filter == 4);
    filter.type_mask = 1;
    assert(radio_rx_subscribe(&b, &subscribers[1], &contexts[1], &filter, &second) == ESP_OK);
    assert(contexts[0].accepted && !contexts[1].calls && driver_packet_filter == 5);
    assert(esp32_mquickjs_wifi_radio_acquire_promiscuous(&csi, &enable) == ESP_OK);
    assert(driver_packet_filter == 7 && driver_rx_callback); /* include saved CSI requirements */
    esp32_mquickjs_wifi_radio_promiscuous_lease_t stale = first;
    esp32_mquickjs_wifi_radio_release_promiscuous(&first);
    assert(!first.acquired && driver_packet_filter == 7 && driver_promiscuous);
    memset(&contexts[0], 0, sizeof(contexts[0]));
    filter.type_mask = 2; filter.subtype_filter = true; filter.subtype_mask = 1U << 13;
    assert(radio_rx_subscribe(&a, &subscribers[0], &contexts[0], &filter, &first) == ESP_OK);
    assert(!contexts[0].calls && driver_control_filter == WIFI_PROMIS_CTRL_FILTER_MASK_ACK);
    esp32_mquickjs_wifi_radio_release_promiscuous(&stale);
    assert(first.acquired && driver_control_filter == WIFI_PROMIS_CTRL_FILTER_MASK_ACK);
    esp32_mquickjs_wifi_radio_release_promiscuous(&enable);
    assert(!enable.acquired && driver_packet_filter == 3 && driver_promiscuous && driver_rx_callback);
    esp32_mquickjs_wifi_radio_release_promiscuous(&first);
    assert(driver_packet_filter == 1 && driver_control_filter == 0 && driver_promiscuous);
    wifi_promiscuous_cb_t late = driver_rx_callback;
    esp32_mquickjs_wifi_radio_release_promiscuous(&second);
    assert(!driver_promiscuous && !driver_rx_callback && driver_packet_filter == 7 && driver_control_filter == 0);
    unsigned before = contexts[0].calls + contexts[1].calls;
    late(rx_packet, WIFI_PKT_DATA); assert(contexts[0].calls + contexts[1].calls == before);
    esp32_mquickjs_wifi_radio_release(&a); esp32_mquickjs_wifi_radio_release(&b); esp32_mquickjs_wifi_radio_release(&csi);
}

static void test_promiscuous_rx_drain(void)
{
    rx_test_enabled = true;
    esp32_mquickjs_wifi_radio_lease_t a = acquire(), b = acquire();
    assert(esp32_mquickjs_wifi_radio_ensure_started(&a) == ESP_OK);
    esp32_mquickjs_wifi_radio_promiscuous_lease_t first, second;
    esp32_mquickjs_wifi_promiscuous_subscriber_t subscribers[2];
    radio_rx_context_t contexts[2] = {{.block = true}, {0}};
    esp32_mquickjs_wifi_rx_filter_t filter = { .type_mask = 4, .sample_every = 1, .valid_only = true };
    assert(radio_rx_subscribe(&a, &subscribers[0], &contexts[0], &filter, &first) == ESP_OK);
    assert(radio_rx_subscribe(&b, &subscribers[1], &contexts[1], &filter, &second) == ESP_OK);
    rx_worker_callback = driver_rx_callback;
    pthread_t worker; assert(!pthread_create(&worker, NULL, radio_rx_worker, NULL));
    pthread_mutex_lock(&rx_lock);
    while (!rx_paused) pthread_cond_wait(&rx_changed, &rx_lock);
    pthread_mutex_unlock(&rx_lock);
    esp32_mquickjs_wifi_radio_release_promiscuous(&first);
    assert(first.acquired && driver_promiscuous);
    int writes = calls[8] + calls[17] + calls[18] + calls[19];
    esp32_mquickjs_wifi_radio_release(&a); assert(a.acquired);
    assert(writes == calls[8] + calls[17] + calls[18] + calls[19]);
    esp32_mquickjs_wifi_radio_status_t status;
    assert(esp32_mquickjs_wifi_radio_get_status(&status) == ESP_OK && !status.cleanup_stage);
    esp32_mquickjs_wifi_promiscuous_snapshot_t snapshot;
    esp32_mquickjs_wifi_promiscuous_snapshot(&snapshot);
    assert(snapshot.closing == 1 && snapshot.active == 1 && snapshot.entered == 2);
    pthread_mutex_lock(&rx_lock); rx_proceed = true; pthread_cond_broadcast(&rx_changed); pthread_mutex_unlock(&rx_lock);
    assert(!pthread_join(worker, NULL));
    esp32_mquickjs_wifi_radio_release_promiscuous(&first); assert(!first.acquired);
    esp32_mquickjs_wifi_radio_release(&a); assert(!a.acquired);
    driver_rx_callback(rx_packet, WIFI_PKT_DATA);
    assert(contexts[0].accepted == 1 && contexts[1].accepted == 2);
    esp32_mquickjs_wifi_radio_release_promiscuous(&second); esp32_mquickjs_wifi_radio_release(&b);
    assert(!driver_promiscuous && !driver_rx_callback);
}

static void test_promiscuous_rx_failure(void)
{
    rx_test_enabled = rx_synchronous = true;
    esp32_mquickjs_wifi_radio_lease_t a = acquire(), b = acquire();
    assert(esp32_mquickjs_wifi_radio_ensure_started(&a) == ESP_OK);
    esp32_mquickjs_wifi_radio_promiscuous_lease_t first, second;
    esp32_mquickjs_wifi_promiscuous_subscriber_t subscribers[2];
    radio_rx_context_t contexts[2] = {0};
    esp32_mquickjs_wifi_rx_filter_t filter = { .type_mask = 4, .sample_every = 1, .valid_only = true };
    assert(radio_rx_subscribe(&a, &subscribers[0], &contexts[0], &filter, &first) == ESP_OK);
    filter.type_mask = 1; fail_at = 17;
    assert(radio_rx_subscribe(&b, &subscribers[1], &contexts[1], &filter, &second) == 0x711);
    assert(second.acquired && !contexts[1].calls && driver_promiscuous && driver_packet_filter == 4);
    esp32_mquickjs_wifi_promiscuous_snapshot_t snapshot;
    esp32_mquickjs_wifi_promiscuous_snapshot(&snapshot);
    assert(snapshot.active == 1 && snapshot.closing == 1 && s_radio.cleanup_stage);
    fail_at = 0;
    esp32_mquickjs_wifi_radio_release_promiscuous(&second);
    assert(!second.acquired && !s_radio.cleanup_stage && driver_promiscuous && driver_packet_filter == 4);
    unsigned accepted = contexts[0].accepted; driver_rx_callback(rx_packet, WIFI_PKT_DATA);
    assert(contexts[0].accepted == accepted + 1 && !contexts[1].calls);
    fail_at = 19;
    esp32_mquickjs_wifi_radio_release_promiscuous(&first);
    assert(first.acquired && s_radio.cleanup_stage);
    accepted = contexts[0].accepted; driver_rx_callback(rx_packet, WIFI_PKT_DATA);
    assert(contexts[0].accepted == accepted);
    fail_at = 0; esp32_mquickjs_wifi_radio_release_promiscuous(&first);
    assert(!first.acquired && !driver_promiscuous && !driver_rx_callback && !s_radio.cleanup_stage);
    esp32_mquickjs_wifi_radio_release(&a); esp32_mquickjs_wifi_radio_release(&b);
}

static void test_promiscuous_rx_capacity(void)
{
    rx_test_enabled = true;
    esp32_mquickjs_wifi_radio_lease_t radio[9];
    esp32_mquickjs_wifi_radio_promiscuous_lease_t tokens[9];
    esp32_mquickjs_wifi_promiscuous_subscriber_t subscribers[9];
    radio_rx_context_t contexts[9] = {0};
    esp32_mquickjs_wifi_rx_filter_t filter = { .type_mask = 4, .sample_every = 1, .valid_only = true };
    for (unsigned i = 0; i < 9; ++i) radio[i] = acquire();
    assert(esp32_mquickjs_wifi_radio_ensure_started(&radio[0]) == ESP_OK);
    for (unsigned i = 0; i < 8; ++i) assert(radio_rx_subscribe(&radio[i], &subscribers[i], &contexts[i], &filter, &tokens[i]) == ESP_OK);
    int before[21]; memcpy(before, calls, sizeof(calls));
    assert(radio_rx_subscribe(&radio[8], &subscribers[8], &contexts[8], &filter, &tokens[8]) == ESP_ERR_NO_MEM);
    assert(!tokens[8].acquired && !memcmp(before, calls, sizeof(calls)));
    for (unsigned i = 0; i < 9; ++i) esp32_mquickjs_wifi_radio_release(&radio[i]);
    assert(!driver_promiscuous && !driver_rx_callback);
}

static void test_registry_capacity(void)
{
    esp32_mquickjs_wifi_radio_lease_t leases[16], extra;
    for (unsigned i = 0; i < 16; ++i) leases[i] = acquire();
    assert(esp32_mquickjs_wifi_radio_acquire(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_ESPNOW, WIFI_MODE_STA, &extra) == ESP_ERR_NO_MEM);
    uint32_t old = leases[3].identity;
    esp32_mquickjs_wifi_radio_release(&leases[3]);
    extra = acquire();
    assert(extra.identity != old);
}

static void test_shared_channel_release(void)
{
    esp32_mquickjs_wifi_radio_lease_t a = acquire(), b = acquire(), stale = a;
    esp32_mquickjs_wifi_radio_status_t status;
    assert(esp32_mquickjs_wifi_radio_ensure_started(&a) == ESP_OK);
    assert(esp32_mquickjs_wifi_radio_set_channel(&a, 6, WIFI_SECOND_CHAN_NONE) == ESP_OK);
    assert(esp32_mquickjs_wifi_radio_set_channel(&b, 6, WIFI_SECOND_CHAN_NONE) == ESP_OK);
    esp32_mquickjs_wifi_radio_release(&a);
    esp32_mquickjs_wifi_radio_release_channel(&stale);
    esp32_mquickjs_wifi_radio_get_status(&status);
    assert(status.fixed_channel_owners == 1 && driver_channel == 6);
    assert(calls[7] == 1);
    esp32_mquickjs_wifi_radio_lease_t c = acquire();
    assert(esp32_mquickjs_wifi_radio_set_channel(&c, 11, WIFI_SECOND_CHAN_NONE) == ESP_ERR_INVALID_STATE);
    assert(esp32_mquickjs_wifi_radio_set_channel(&c, 6, WIFI_SECOND_CHAN_ABOVE) == ESP_ERR_INVALID_STATE);
    assert(calls[7] == 1);
    esp32_mquickjs_wifi_radio_release(&b);
    assert(esp32_mquickjs_wifi_radio_set_channel(&c, 11, WIFI_SECOND_CHAN_NONE) == ESP_OK);
}

static void test_lifecycle(void)
{
    esp32_mquickjs_wifi_radio_status_t status;
    esp32_mquickjs_wifi_radio_lease_t a = acquire(), old = a;
    assert(esp32_mquickjs_wifi_radio_ensure_started(&a) == ESP_OK);
    assert(esp32_mquickjs_wifi_radio_stop() == ESP_ERR_INVALID_STATE);
    assert(esp32_mquickjs_wifi_radio_shutdown() == ESP_ERR_INVALID_STATE);
    assert(!calls[9] && !calls[10]);
    esp32_mquickjs_wifi_radio_release(&a);
    assert(esp32_mquickjs_wifi_radio_stop() == ESP_OK);
    assert(esp32_mquickjs_wifi_radio_stop() == ESP_OK);
    esp32_mquickjs_wifi_radio_get_status(&status);
    assert(status.driver_state == ESP32_MQUICKJS_WIFI_RADIO_STOPPED && !status.started && status.initialized);
    a = acquire();
    assert(esp32_mquickjs_wifi_radio_ensure_started(&a) == ESP_OK);
    assert(calls[2] == 1 && calls[6] == 2 && calls[9] == 1);
    esp32_mquickjs_wifi_radio_release(&a);
    assert(esp32_mquickjs_wifi_radio_shutdown() == ESP_OK);
    assert(esp32_mquickjs_wifi_radio_shutdown() == ESP_OK);
    assert(calls[9] == 2 && calls[10] == 1);
    a = acquire();
    assert(a.generation != old.generation && a.identity != old.identity);
    assert(esp32_mquickjs_wifi_radio_ensure_started(&old) == ESP_ERR_INVALID_STATE);
    assert(esp32_mquickjs_wifi_radio_ensure_started(&a) == ESP_OK);
    assert(calls[2] == 2 && calls[6] == 3);
}

static void test_cleanup_suffix(int stage)
{
    esp32_mquickjs_wifi_radio_lease_t a = acquire();
    esp32_mquickjs_wifi_radio_status_t status;
    assert(esp32_mquickjs_wifi_radio_ensure_started(&a) == ESP_OK);
    esp32_mquickjs_wifi_radio_release(&a);
    fail_at = stage;
    assert(esp32_mquickjs_wifi_radio_shutdown() == 0x700 + stage);
    esp32_mquickjs_wifi_radio_get_status(&status);
    assert(status.driver_state == ESP32_MQUICKJS_WIFI_RADIO_CLEANUP_PENDING);
    assert(status.cleanup_error == 0x700 + stage && status.driver_owned);
    assert(status.started == (stage == 9));
    assert(esp32_mquickjs_wifi_radio_acquire(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_ESPNOW, WIFI_MODE_STA, &a) == 0x700 + stage);
    int stops = calls[9];
    fail_at = 0;
    assert(esp32_mquickjs_wifi_radio_shutdown() == ESP_OK);
    assert(calls[9] == stops + (stage == 9));
    esp32_mquickjs_wifi_radio_get_status(&status);
    assert(status.driver_state == ESP32_MQUICKJS_WIFI_RADIO_UNINITIALIZED && !status.driver_owned);
    a = acquire();
    assert(esp32_mquickjs_wifi_radio_ensure_started(&a) == ESP_OK);
}

static void test_failed_start_cleanup(int stage)
{
    esp32_mquickjs_wifi_radio_lease_t a = acquire();
    fail_at = stage;
    assert(esp32_mquickjs_wifi_radio_ensure_started(&a) == 0x700 + stage);
    esp32_mquickjs_wifi_radio_release(&a);
    fail_at = 0;
    if (stage <= 2) {
        assert(esp32_mquickjs_wifi_radio_shutdown() == 0x700 + stage);
        assert(!calls[10]);
        return;
    }
    assert(esp32_mquickjs_wifi_radio_shutdown() == ESP_OK);
    assert(calls[10] == 1 && calls[9] == (stage == 6));
    a = acquire();
    assert(esp32_mquickjs_wifi_radio_ensure_started(&a) == ESP_OK);
}

static void test_mode_union(void)
{
    esp32_mquickjs_wifi_radio_lease_t sta = acquire(), ap;
    esp32_mquickjs_wifi_radio_status_t status;
    assert(esp32_mquickjs_wifi_radio_acquire(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_CSI, WIFI_MODE_AP, &ap) == ESP_OK);
    assert(esp32_mquickjs_wifi_radio_ensure_started(&sta) == ESP_OK);
    assert(driver_mode == WIFI_MODE_APSTA);
    esp32_mquickjs_wifi_radio_release(&ap);
    assert(esp32_mquickjs_wifi_radio_ensure_started(&sta) == ESP_OK);
    esp32_mquickjs_wifi_radio_get_status(&status);
    assert(status.requested_mode == WIFI_MODE_STA && status.mode == WIFI_MODE_APSTA);
    esp32_mquickjs_wifi_radio_release(&sta);
    assert(esp32_mquickjs_wifi_radio_stop() == ESP_OK);
    sta = acquire();
    assert(esp32_mquickjs_wifi_radio_ensure_started(&sta) == ESP_OK);
    assert(driver_mode == WIFI_MODE_STA);
}

static void test_home_channel_and_drift(void)
{
    esp32_mquickjs_wifi_radio_lease_t a = acquire(), b = acquire();
    esp32_mquickjs_wifi_radio_status_t status;
    assert(esp32_mquickjs_wifi_radio_ensure_started(&a) == ESP_OK);
    assert(esp32_mquickjs_wifi_radio_set_channel(&a, 6, WIFI_SECOND_CHAN_NONE) == ESP_OK);
    driver_channel = 11;station_connected = true;
    assert(esp32_mquickjs_wifi_radio_set_channel(&b, 6, WIFI_SECOND_CHAN_NONE) == ESP_ERR_INVALID_STATE);
    esp32_mquickjs_wifi_radio_get_status(&status);
    assert(status.primary_channel == 11 && status.channel_generation == 3);
    assert(status.conflicted_channel_owners == 1 && calls[7] == 1);
    esp32_mquickjs_wifi_radio_release_channel(&a);
    assert(esp32_mquickjs_wifi_radio_set_channel(&b, 6, WIFI_SECOND_CHAN_NONE) == ESP_ERR_INVALID_STATE);
    assert(esp32_mquickjs_wifi_radio_set_channel(&b, 11, WIFI_SECOND_CHAN_ABOVE) == ESP_ERR_INVALID_STATE);
    assert(esp32_mquickjs_wifi_radio_set_channel(&b, 11, WIFI_SECOND_CHAN_NONE) == ESP_OK);
    assert(calls[7] == 1);
}

static void *shutdown_thread(void *opaque)
{
    (void)opaque;
    assert(esp32_mquickjs_wifi_radio_shutdown() == ESP_OK);
    return NULL;
}

static void *acquire_thread(void *opaque)
{
    *(esp32_mquickjs_wifi_radio_lease_t *)opaque = acquire();
    return NULL;
}

static void test_shutdown_acquire_race(int stage)
{
    esp32_mquickjs_wifi_radio_lease_t a = acquire(), b;
    assert(esp32_mquickjs_wifi_radio_ensure_started(&a) == ESP_OK);
    uint32_t old_generation = a.generation;
    esp32_mquickjs_wifi_radio_release(&a);
    pause_at = stage;
    pthread_t closing, acquiring;
    pthread_create(&closing, NULL, shutdown_thread, NULL);
    pthread_mutex_lock(&schedule_lock);
    while (!paused) pthread_cond_wait(&schedule_cond, &schedule_lock);
    pthread_mutex_unlock(&schedule_lock);
    pthread_create(&acquiring, NULL, acquire_thread, &b);
    pthread_mutex_lock(&schedule_lock);
    while (!mutex_contended) pthread_cond_wait(&schedule_cond, &schedule_lock);
    resume_driver = 1;
    pthread_cond_broadcast(&schedule_cond);
    pthread_mutex_unlock(&schedule_lock);
    pthread_join(closing, NULL);
    pthread_join(acquiring, NULL);
    assert(b.generation != old_generation && calls[9] == 1 && calls[10] == 1);
    assert(esp32_mquickjs_wifi_radio_ensure_started(&b) == ESP_OK);
}


static void test_global_settings(void)
{
    esp32_mquickjs_wifi_radio_lease_t a = acquire(), old = a;
    wifi_ps_type_t ps = WIFI_PS_NONE;
    int8_t power = 0;
    assert(esp32_mquickjs_wifi_radio_set_power_save(&a, WIFI_PS_MIN_MODEM, &ps) == ESP_ERR_INVALID_STATE);
    assert(esp32_mquickjs_wifi_radio_ensure_started(&a) == ESP_OK);
    assert(esp32_mquickjs_wifi_radio_set_power_save(&a, (wifi_ps_type_t)99, &ps) == ESP_ERR_INVALID_ARG);
    assert(esp32_mquickjs_wifi_radio_set_tx_power(&a, 7, &power) == ESP_ERR_INVALID_ARG);
    assert(esp32_mquickjs_wifi_radio_set_tx_power(&a, 81, &power) == ESP_ERR_INVALID_ARG);
    assert(esp32_mquickjs_wifi_radio_set_tx_power(&a, 40, NULL) == ESP_ERR_INVALID_ARG);
    assert(calls[11] == 0 && calls[13] == 0);
    assert(esp32_mquickjs_wifi_radio_set_power_save(&a, WIFI_PS_MIN_MODEM, &ps) == ESP_OK);
    assert(ps == WIFI_PS_MIN_MODEM);
    assert(esp32_mquickjs_wifi_radio_set_tx_power(&a, 80, &power) == ESP_OK);
    assert(power == 78); /* Return driver quantization, not the request. */
    esp32_mquickjs_wifi_radio_lease_t b = acquire();
    assert(esp32_mquickjs_wifi_radio_set_channel(&b, 6, WIFI_SECOND_CHAN_NONE) == ESP_OK);
    assert(esp32_mquickjs_wifi_radio_set_power_save(&a, WIFI_PS_NONE, &ps) == ESP_ERR_INVALID_STATE);
    assert(esp32_mquickjs_wifi_radio_set_tx_power(&a, 40, &power) == ESP_ERR_INVALID_STATE);
    assert(calls[11] == 1 && calls[13] == 1 && ps == WIFI_PS_MIN_MODEM && power == 78);
    esp32_mquickjs_wifi_radio_release(&b);
    esp32_mquickjs_wifi_radio_release(&a);
    assert(esp32_mquickjs_wifi_radio_set_power_save(&old, WIFI_PS_NONE, &ps) == ESP_ERR_INVALID_STATE);
    assert(esp32_mquickjs_wifi_radio_set_tx_power(&old, 40, &power) == ESP_ERR_INVALID_STATE);
    assert(calls[11] == 1 && calls[13] == 1);
}

static void test_global_settings_errors(void)
{
    esp32_mquickjs_wifi_radio_lease_t a = acquire();
    wifi_ps_type_t ps = WIFI_PS_MAX_MODEM;
    int8_t power = -1;
    assert(esp32_mquickjs_wifi_radio_ensure_started(&a) == ESP_OK);
    for (int stage = 11; stage <= 14; ++stage) {
        fail_at = stage;
        esp_err_t err = stage < 13
            ? esp32_mquickjs_wifi_radio_set_power_save(&a, WIFI_PS_MIN_MODEM, &ps)
            : esp32_mquickjs_wifi_radio_set_tx_power(&a, 40, &power);
        assert(err == 0x700 + stage && ps == WIFI_PS_MAX_MODEM && power == -1);
        if (stage == 11) assert(calls[12] == 0 && driver_ps == WIFI_PS_NONE);
        if (stage == 12) assert(driver_ps == WIFI_PS_MIN_MODEM);
        if (stage == 13) assert(calls[14] == 0 && driver_power == 80);
        if (stage == 14) assert(driver_power == 40);
    }
    fail_at = 0;
    esp32_mquickjs_wifi_radio_promiscuous_lease_t prom;
    assert(esp32_mquickjs_wifi_radio_acquire_promiscuous(&a, &prom) == ESP_OK);
    assert(esp32_mquickjs_wifi_radio_set_tx_power(&a, 40, &power) == ESP_ERR_INVALID_STATE);
    fail_at = 8;
    esp32_mquickjs_wifi_radio_release_promiscuous(&prom);
    int before[21];memcpy(before, calls, sizeof(calls));
    assert(esp32_mquickjs_wifi_radio_set_power_save(&a, WIFI_PS_NONE, &ps) == 0x708);
    assert(esp32_mquickjs_wifi_radio_set_tx_power(&a, 40, &power) == 0x708);
    assert(!memcmp(before, calls, sizeof(calls)));
}

static void *setting_thread(void *opaque)
{
    wifi_ps_type_t ps;
    int8_t power;
    if (pause_at == 11) {
        assert(esp32_mquickjs_wifi_radio_set_power_save(opaque, WIFI_PS_MIN_MODEM, &ps) == ESP_OK);
        assert(ps == WIFI_PS_MIN_MODEM);
    } else {
        assert(esp32_mquickjs_wifi_radio_set_tx_power(opaque, 40, &power) == ESP_OK);
        assert(power == 40);
    }
    return NULL;
}

static void test_setting_release_race(int stage)
{
    esp32_mquickjs_wifi_radio_lease_t a = acquire(), copy = a;
    assert(esp32_mquickjs_wifi_radio_ensure_started(&a) == ESP_OK);
    pause_at = stage;
    pthread_t setting, releasing;
    pthread_create(&setting, NULL, setting_thread, &a);
    pthread_mutex_lock(&schedule_lock);
    while (!paused) pthread_cond_wait(&schedule_cond, &schedule_lock);
    pthread_mutex_unlock(&schedule_lock);
    pthread_create(&releasing, NULL, release_thread, &copy);
    pthread_mutex_lock(&schedule_lock);
    while (!mutex_contended && !atomic_load(&release_finished))
        pthread_cond_wait(&schedule_cond, &schedule_lock);
    assert(!atomic_load(&release_finished));
    resume_driver = 1;pthread_cond_broadcast(&schedule_cond);
    pthread_mutex_unlock(&schedule_lock);
    pthread_join(setting, NULL);pthread_join(releasing, NULL);
    assert(!wifi_radio_lease_valid(&a) && calls[stage] == 1 && calls[stage + 1] == 1);
}

static esp32_mquickjs_wifi_radio_lease_t acquire_sta(void)
{
    esp32_mquickjs_wifi_radio_lease_t lease;
    assert(esp32_mquickjs_wifi_radio_acquire(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_STA,
                                           WIFI_MODE_STA, &lease) == ESP_OK);
    assert(esp32_mquickjs_wifi_radio_ensure_started(&lease) == ESP_OK);
    return lease;
}

static void test_operation_fixed_conflict(void)
{
    esp32_mquickjs_wifi_radio_lease_t sta = acquire_sta(), fixed = acquire();
    esp32_mquickjs_wifi_radio_operation_t operation = {0};
    assert(esp32_mquickjs_wifi_radio_set_channel(&fixed, 6, WIFI_SECOND_CHAN_NONE) == ESP_OK);
    for (int kind = ESP32_MQUICKJS_WIFI_RADIO_OPERATION_SCAN;
         kind <= ESP32_MQUICKJS_WIFI_RADIO_OPERATION_CONNECT; ++kind) {
        assert(esp32_mquickjs_wifi_radio_begin_operation(&sta, kind, &operation) == ESP_ERR_INVALID_STATE);
        assert(!operation.identity && calls[7] == 1);
    }
    esp32_mquickjs_wifi_radio_release_channel(&fixed);
    assert(esp32_mquickjs_wifi_radio_begin_operation(&sta, ESP32_MQUICKJS_WIFI_RADIO_OPERATION_SCAN, &operation) == ESP_OK);
    assert(esp32_mquickjs_wifi_radio_set_channel(&fixed, 6, WIFI_SECOND_CHAN_NONE) == ESP_ERR_INVALID_STATE);
    assert(esp32_mquickjs_wifi_radio_set_channel(&fixed, 11, WIFI_SECOND_CHAN_NONE) == ESP_ERR_INVALID_STATE);
    assert(calls[7] == 1);
    esp32_mquickjs_wifi_radio_status_t status;
    assert(esp32_mquickjs_wifi_radio_get_status(&status) == ESP_OK && status.active_operations == 1);
    esp32_mquickjs_wifi_radio_end_operation(&operation);
    assert(esp32_mquickjs_wifi_radio_get_status(&status) == ESP_OK && !status.active_operations);
    assert(esp32_mquickjs_wifi_radio_set_channel(&fixed, 11, WIFI_SECOND_CHAN_NONE) == ESP_OK);
}

static void test_operation_identity_and_owner(void)
{
    esp32_mquickjs_wifi_radio_lease_t sta = acquire_sta(), copy = sta;
    esp32_mquickjs_wifi_radio_operation_t operation = {0}, next = {0};
    assert(esp32_mquickjs_wifi_radio_begin_operation(&sta, ESP32_MQUICKJS_WIFI_RADIO_OPERATION_CONNECT, &operation) == ESP_OK);
    esp32_mquickjs_wifi_radio_operation_t old = operation, forged = operation;
    forged.lease_identity++;
    esp32_mquickjs_wifi_radio_end_operation(&forged);
    esp32_mquickjs_wifi_radio_release(&copy);
    assert(copy.acquired && wifi_radio_lease_valid(&copy));
    assert(esp32_mquickjs_wifi_radio_shutdown() == ESP_ERR_INVALID_STATE);
    assert(esp32_mquickjs_wifi_radio_begin_operation(&sta, ESP32_MQUICKJS_WIFI_RADIO_OPERATION_SCAN, &next) == ESP_ERR_INVALID_STATE);
    esp32_mquickjs_wifi_radio_end_operation(&operation);
    assert(esp32_mquickjs_wifi_radio_begin_operation(&sta, ESP32_MQUICKJS_WIFI_RADIO_OPERATION_SCAN, &next) == ESP_OK);
    assert(next.identity != old.identity);
    esp32_mquickjs_wifi_radio_end_operation(&old);
    esp32_mquickjs_wifi_radio_end_operation(&old);
    assert(s_radio.operation.identity == next.identity);
    esp32_mquickjs_wifi_radio_end_operation(&next);
    esp32_mquickjs_wifi_radio_release(&copy);
    assert(!copy.acquired && !wifi_radio_lease_valid(&sta));
    assert(esp32_mquickjs_wifi_radio_begin_operation(&sta, ESP32_MQUICKJS_WIFI_RADIO_OPERATION_SCAN, &next) == ESP_ERR_INVALID_STATE);
}

static void test_operation_exhaustion(void)
{
    esp32_mquickjs_wifi_radio_lease_t sta = acquire_sta();
    esp32_mquickjs_wifi_radio_operation_t operation = {0};
    s_radio.next_operation_identity = UINT32_MAX;
    assert(esp32_mquickjs_wifi_radio_begin_operation(&sta, ESP32_MQUICKJS_WIFI_RADIO_OPERATION_SCAN, &operation) == ESP_OK);
    assert(operation.identity == UINT32_MAX);
    esp32_mquickjs_wifi_radio_end_operation(&operation);
    esp32_mquickjs_wifi_radio_release(&sta);
    assert(esp32_mquickjs_wifi_radio_shutdown() == ESP_OK);
    sta = acquire_sta();
    assert(esp32_mquickjs_wifi_radio_begin_operation(&sta, ESP32_MQUICKJS_WIFI_RADIO_OPERATION_CONNECT, &operation) == ESP_ERR_NO_MEM);
    assert(!operation.identity);
}

static void test_operation_mutation_guard(void)
{
    esp32_mquickjs_wifi_radio_lease_t sta = acquire_sta(), ap;
    esp32_mquickjs_wifi_radio_operation_t operation = {0};
    assert(esp32_mquickjs_wifi_radio_begin_operation(&sta, ESP32_MQUICKJS_WIFI_RADIO_OPERATION_CONNECT, &operation) == ESP_OK);
    wifi_ps_type_t ps;
    int8_t power;
    assert(esp32_mquickjs_wifi_radio_set_power_save(&sta, WIFI_PS_NONE, &ps) == ESP_ERR_INVALID_STATE);
    assert(esp32_mquickjs_wifi_radio_set_tx_power(&sta, 40, &power) == ESP_ERR_INVALID_STATE);
    assert(!calls[11] && !calls[13]);
    assert(esp32_mquickjs_wifi_radio_acquire(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_CSI, WIFI_MODE_AP, &ap) == ESP_OK);
    int modes = calls[5];
    assert(esp32_mquickjs_wifi_radio_ensure_started(&ap) == ESP_ERR_INVALID_STATE);
    assert(calls[5] == modes && driver_mode == WIFI_MODE_STA);
    esp32_mquickjs_wifi_radio_end_operation(&operation);
    assert(esp32_mquickjs_wifi_radio_ensure_started(&ap) == ESP_OK && driver_mode == WIFI_MODE_APSTA);
}

static void *fixed_channel_thread(void *opaque)
{
    assert(esp32_mquickjs_wifi_radio_set_channel(opaque, 6, WIFI_SECOND_CHAN_NONE) == ESP_OK);
    return NULL;
}
static void *admission_thread(void *opaque)
{
    esp32_mquickjs_wifi_radio_operation_t operation = {0};
    assert(esp32_mquickjs_wifi_radio_begin_operation(opaque, ESP32_MQUICKJS_WIFI_RADIO_OPERATION_SCAN, &operation) == ESP_ERR_INVALID_STATE);
    assert(!operation.identity);
    return NULL;
}
static void test_operation_channel_race(void)
{
    esp32_mquickjs_wifi_radio_lease_t sta = acquire_sta(), fixed = acquire();
    pause_at = 7;
    pthread_t setting, admitting;
    pthread_create(&setting, NULL, fixed_channel_thread, &fixed);
    pthread_mutex_lock(&schedule_lock);
    while (!paused) pthread_cond_wait(&schedule_cond, &schedule_lock);
    pthread_mutex_unlock(&schedule_lock);
    pthread_create(&admitting, NULL, admission_thread, &sta);
    pthread_mutex_lock(&schedule_lock);
    while (!mutex_contended) pthread_cond_wait(&schedule_cond, &schedule_lock);
    resume_driver = 1;pthread_cond_broadcast(&schedule_cond);
    pthread_mutex_unlock(&schedule_lock);
    pthread_join(setting, NULL);pthread_join(admitting, NULL);
    assert(calls[7] == 1 && !s_radio.operation.identity);
}

static void test_channel_event_conflict(void)
{
    esp32_mquickjs_wifi_radio_lease_t fixed = acquire(), following = acquire();
    assert(esp32_mquickjs_wifi_radio_ensure_started(&fixed) == ESP_OK);
    assert(esp32_mquickjs_wifi_radio_set_channel(&fixed, 6, WIFI_SECOND_CHAN_NONE) == ESP_OK);
    driver_channel = 11;
    channel_handler(NULL, WIFI_EVENT, WIFI_EVENT_HOME_CHANNEL_CHANGE, NULL);
    esp32_mquickjs_wifi_radio_channel_status_t status;
    assert(esp32_mquickjs_wifi_radio_lease_channel_status(&fixed, &status) == ESP_OK);
    assert(status.primary == 11 && status.fixed && status.conflicted);
    assert(esp32_mquickjs_wifi_radio_lease_channel_status(&following, &status) == ESP_OK);
    assert(status.primary == 11 && !status.fixed && !status.conflicted);
    driver_channel = 6;
    channel_handler(NULL, WIFI_EVENT, WIFI_EVENT_STA_CONNECTED, NULL);
    assert(esp32_mquickjs_wifi_radio_lease_channel_status(&fixed, &status) == ESP_OK && status.conflicted);
    assert(calls[7] == 1); /* Never force the home channel back. */
    esp32_mquickjs_wifi_radio_release_channel(&fixed);
    assert(esp32_mquickjs_wifi_radio_lease_channel_status(&fixed, &status) == ESP_OK && !status.conflicted);
    esp32_mquickjs_wifi_radio_lease_t old = fixed;
    esp32_mquickjs_wifi_radio_release(&fixed);
    assert(esp32_mquickjs_wifi_radio_lease_channel_status(&old, &status) == ESP_ERR_INVALID_STATE);
}

static void test_channel_event_error_and_restart(void)
{
    esp32_mquickjs_wifi_radio_lease_t lease = acquire_sta();
    channel_read_error = 0x799;
    channel_handler(NULL, WIFI_EVENT, WIFI_EVENT_HOME_CHANNEL_CHANGE, NULL);
    esp32_mquickjs_wifi_radio_channel_status_t status;
    assert(esp32_mquickjs_wifi_radio_lease_channel_status(&lease, &status) == 0x799);
    channel_read_error = 0;driver_channel = 6;
    channel_handler(NULL, WIFI_EVENT, WIFI_EVENT_HOME_CHANNEL_CHANGE, NULL);
    assert(esp32_mquickjs_wifi_radio_lease_channel_status(&lease, &status) == ESP_OK && status.primary == 6);
    esp32_mquickjs_wifi_radio_release(&lease);
    assert(esp32_mquickjs_wifi_radio_shutdown() == ESP_OK);
    channel_handler(NULL, WIFI_EVENT, WIFI_EVENT_HOME_CHANNEL_CHANGE, NULL);
    assert(!s_radio.primary_channel);
    lease = acquire_sta();
    int obsolete_payload = 11;
    channel_handler(NULL, WIFI_EVENT, WIFI_EVENT_HOME_CHANNEL_CHANGE, &obsolete_payload);
    assert(esp32_mquickjs_wifi_radio_lease_channel_status(&lease, &status) == ESP_OK && status.primary == 6);
    assert(handler_registrations == 2);
}

static void *stale_channel_thread(void *opaque)
{
    (void)opaque;pause_channel_read = true;
    uint8_t primary;wifi_second_chan_t secondary;uint32_t generation;
    assert(esp32_mquickjs_wifi_radio_get_channel(&primary, &secondary, &generation) == ESP_OK);
    assert(primary == 11);
    return NULL;
}
static void test_channel_event_overtakes_query(void)
{
    esp32_mquickjs_wifi_radio_lease_t lease = acquire_sta();
    (void)lease;
    pause_at = 15;driver_channel = 6;
    pthread_t reading;pthread_create(&reading, NULL, stale_channel_thread, NULL);
    pthread_mutex_lock(&schedule_lock);
    while (!paused) pthread_cond_wait(&schedule_cond, &schedule_lock);
    pthread_mutex_unlock(&schedule_lock);
    driver_channel = 11;
    /* Event callback cannot wait on the mutation mutex held by reading. */
    channel_handler(NULL, WIFI_EVENT, WIFI_EVENT_HOME_CHANNEL_CHANGE, NULL);
    pthread_mutex_lock(&schedule_lock);
    resume_driver = 1;pthread_cond_broadcast(&schedule_cond);
    pthread_mutex_unlock(&schedule_lock);
    pthread_join(reading, NULL);
    assert(s_radio.primary_channel == 11);
}

static void *channel_write_overtaken(void *opaque)
{
    assert(esp32_mquickjs_wifi_radio_set_channel(opaque, 6, WIFI_SECOND_CHAN_NONE) == ESP_ERR_INVALID_STATE);
    return NULL;
}
static void test_channel_event_overtakes_write(void)
{
    esp32_mquickjs_wifi_radio_lease_t lease = acquire_sta();
    pause_at = 7;
    pthread_t writing;pthread_create(&writing, NULL, channel_write_overtaken, &lease);
    pthread_mutex_lock(&schedule_lock);
    while (!paused) pthread_cond_wait(&schedule_cond, &schedule_lock);
    pthread_mutex_unlock(&schedule_lock);
    driver_channel = 11;
    channel_handler(NULL, WIFI_EVENT, WIFI_EVENT_HOME_CHANNEL_CHANGE, NULL);
    pthread_mutex_lock(&schedule_lock);
    resume_driver = 1;pthread_cond_broadcast(&schedule_cond);
    pthread_mutex_unlock(&schedule_lock);
    pthread_join(writing, NULL);
    esp32_mquickjs_wifi_radio_status_t status;
    assert(esp32_mquickjs_wifi_radio_get_status(&status) == ESP_OK);
    assert(!status.fixed_channel_owners && calls[7] == 1);
    /* SDK write occurred; rejecting its stale owner commit is not rollback. */
    assert(status.primary_channel == driver_channel);
}

static void test_channel_registration_failure(bool loop)
{
    if (loop) loop_error = 0x799; else handler_error = 0x799;
    esp32_mquickjs_wifi_radio_lease_t lease = acquire();
    assert(esp32_mquickjs_wifi_radio_ensure_started(&lease) == 0x799);
    assert(!calls[2] && !s_radio.driver_owned);
    assert(!strcmp(s_radio.fault_stage, loop ? "event-loop" : "channel-handler"));
    esp32_mquickjs_wifi_radio_release(&lease);
    assert(esp32_mquickjs_wifi_radio_shutdown() == ESP_OK);
    loop_error = ESP_ERR_INVALID_STATE;handler_error = 0;
    lease = acquire_sta();
    assert(calls[2] == 1);
}

static void test_retire_shared(void)
{
    esp32_mquickjs_wifi_radio_lease_t sta = acquire_sta(), fixed = acquire();
    assert(esp32_mquickjs_wifi_radio_set_channel(&fixed, 6, WIFI_SECOND_CHAN_NONE) == ESP_OK);
    assert(esp32_mquickjs_wifi_radio_release_and_stop_idle(&sta) == ESP_OK);
    assert(!sta.acquired && wifi_radio_lease_valid(&fixed) && !calls[9]);
    assert(esp32_mquickjs_wifi_radio_release_and_stop_idle(&sta) == ESP_OK);
    assert(!calls[9] && s_radio.started);
    assert(esp32_mquickjs_wifi_radio_release_and_stop_idle(&fixed) == ESP_OK);
    assert(calls[9] == 1 && !s_radio.started && !calls[10]);
}

static void test_retire_operation(void)
{
    esp32_mquickjs_wifi_radio_lease_t sta = acquire_sta();
    esp32_mquickjs_wifi_radio_operation_t op = {0};
    assert(esp32_mquickjs_wifi_radio_begin_operation(&sta, ESP32_MQUICKJS_WIFI_RADIO_OPERATION_SCAN, &op) == ESP_OK);
    assert(esp32_mquickjs_wifi_radio_release_and_stop_idle(&sta) == ESP_ERR_INVALID_STATE);
    assert(sta.acquired && wifi_radio_lease_valid(&sta) && !calls[9]);
    esp32_mquickjs_wifi_radio_end_operation(&op);
    assert(esp32_mquickjs_wifi_radio_release_and_stop_idle(&sta) == ESP_OK);
    assert(!sta.acquired && calls[9] == 1);
}

static void test_retire_stop_error(void)
{
    esp32_mquickjs_wifi_radio_lease_t sta = acquire_sta(), stale = sta;
    fail_at = 9;
    assert(esp32_mquickjs_wifi_radio_release_and_stop_idle(&sta) == 0x709);
    assert(!sta.acquired && calls[9] == 1);
    esp32_mquickjs_wifi_radio_status_t status;
    esp32_mquickjs_wifi_radio_get_status(&status);
    assert(status.cleanup_error == 0x709 && status.driver_state == ESP32_MQUICKJS_WIFI_RADIO_CLEANUP_PENDING);
    assert(esp32_mquickjs_wifi_radio_release_and_stop_idle(&stale) == ESP_ERR_INVALID_STATE);
    assert(calls[9] == 1);
    fail_at = 0;
    assert(esp32_mquickjs_wifi_radio_release_and_stop_idle(&sta) == ESP_OK);
    assert(calls[9] == 2 && !calls[10]);
    assert(esp32_mquickjs_wifi_radio_release_and_stop_idle(&sta) == ESP_OK);
    assert(calls[9] == 2);
}

static void *retire_thread(void *opaque)
{
    assert(esp32_mquickjs_wifi_radio_release_and_stop_idle(opaque) == ESP_OK);
    return NULL;
}

static void test_retire_acquire_race(void)
{
    esp32_mquickjs_wifi_radio_lease_t sta = acquire_sta(), next;
    pause_at = 9;
    pthread_t retiring, acquiring;
    pthread_create(&retiring, NULL, retire_thread, &sta);
    pthread_mutex_lock(&schedule_lock);
    while (!paused) pthread_cond_wait(&schedule_cond, &schedule_lock);
    pthread_mutex_unlock(&schedule_lock);
    pthread_create(&acquiring, NULL, acquire_thread, &next);
    pthread_mutex_lock(&schedule_lock);
    while (!mutex_contended) pthread_cond_wait(&schedule_cond, &schedule_lock);
    resume_driver = 1;
    pthread_cond_broadcast(&schedule_cond);
    pthread_mutex_unlock(&schedule_lock);
    pthread_join(retiring, NULL);
    pthread_join(acquiring, NULL);
    assert(!sta.acquired && next.acquired && calls[9] == 1 && !calls[10]);
    assert(esp32_mquickjs_wifi_radio_ensure_started(&next) == ESP_OK);
    assert(calls[2] == 1 && calls[6] == 2);
}

static void test_lifecycle_reservation(void)
{
    esp32_mquickjs_wifi_radio_lease_t sta = acquire_sta(), app = {0}, other = acquire();
    esp32_mquickjs_wifi_radio_lifecycle_t token = {0};
    assert(esp32_mquickjs_wifi_radio_acquire(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_APPLICATION, WIFI_MODE_STA, &app) == ESP_OK);
    int before[21]; memcpy(before, calls, sizeof(calls));
    assert(esp32_mquickjs_wifi_radio_begin_lifecycle(&app, &sta, NULL, &token) == ESP_ERR_INVALID_STATE);
    assert(!token.identity && !memcmp(before, calls, sizeof(calls)));
    esp32_mquickjs_wifi_radio_release(&other);
    assert(esp32_mquickjs_wifi_radio_begin_lifecycle(&app, &sta, NULL, &token) == ESP_OK);
    esp32_mquickjs_wifi_radio_lifecycle_t stale = token;
    esp32_mquickjs_wifi_radio_operation_t op = {0};
    assert(esp32_mquickjs_wifi_radio_begin_operation(&sta, ESP32_MQUICKJS_WIFI_RADIO_OPERATION_SCAN, &op) == ESP_ERR_INVALID_STATE);
    assert(esp32_mquickjs_wifi_radio_ensure_started(&sta) == ESP_ERR_INVALID_STATE);
    assert(esp32_mquickjs_wifi_radio_acquire(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_ESPNOW, WIFI_MODE_STA, &other) == ESP_ERR_INVALID_STATE);
    assert(esp32_mquickjs_wifi_radio_set_channel(&sta, 6, WIFI_SECOND_CHAN_NONE) == ESP_ERR_INVALID_STATE);
    assert(esp32_mquickjs_wifi_radio_shutdown() == ESP_ERR_INVALID_STATE);
    assert(esp32_mquickjs_wifi_radio_finish_lifecycle(&token, false) == ESP_ERR_INVALID_STATE);
    assert(!memcmp(before, calls, sizeof(calls)));
    assert(esp32_mquickjs_wifi_radio_release_and_stop_idle(&sta) == ESP_OK);
    esp32_mquickjs_wifi_radio_release(&app);
    assert(!calls[9]);
    assert(esp32_mquickjs_wifi_radio_finish_lifecycle(&token, true) == ESP_OK);
    assert(calls[9] == 1 && calls[10] == 1 && unregister_calls == 1);
    assert(esp32_mquickjs_wifi_radio_begin_lifecycle(NULL, NULL, NULL, &token) == ESP_OK);
    assert(esp32_mquickjs_wifi_radio_finish_lifecycle(&stale, false) == ESP_ERR_INVALID_STATE);
    assert(s_radio.lifecycle.identity == token.identity);
    assert(esp32_mquickjs_wifi_radio_finish_lifecycle(&token, false) == ESP_OK);
}

static void test_lifecycle_unregister_suffix(void)
{
    esp32_mquickjs_wifi_radio_lease_t sta = acquire_sta();
    esp32_mquickjs_wifi_radio_lifecycle_t token = {0};
    assert(esp32_mquickjs_wifi_radio_begin_lifecycle(NULL, &sta, NULL, &token) == ESP_OK);
    esp32_mquickjs_wifi_radio_release(&sta);
    unregister_error = -77;
    assert(esp32_mquickjs_wifi_radio_finish_lifecycle(&token, true) == -77);
    assert(token.identity && calls[9] == 1 && !calls[10] && s_channel_event_instance);
    assert(!strcmp(s_radio.cleanup_stage, "channel-unregister"));
    unregister_error = 0;
    assert(esp32_mquickjs_wifi_radio_finish_lifecycle(&token, true) == ESP_OK);
    assert(!token.identity && calls[9] == 1 && calls[10] == 1 && unregister_calls == 2);
}

static void test_lifecycle_restart_failure(int stage)
{
    esp32_mquickjs_wifi_radio_lease_t app = {0};
    esp32_mquickjs_wifi_radio_lifecycle_t token = {0};
    assert(esp32_mquickjs_wifi_radio_begin_lifecycle(NULL, NULL, NULL, &token) == ESP_OK);
    fail_at = stage;
    assert(esp32_mquickjs_wifi_radio_restart_lifecycle(&token, WIFI_MODE_STA, &app) == 0x700 + stage);
    assert(token.identity && !app.acquired);
    assert(esp32_mquickjs_wifi_radio_acquire(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_ESPNOW, WIFI_MODE_STA, &app) == ESP_ERR_INVALID_STATE);
    fail_at = 0;
    int starts = calls[6];
    int err = esp32_mquickjs_wifi_radio_restart_lifecycle(&token, WIFI_MODE_STA, &app);
    if (stage <= 2) {
        assert(err == 0x700 + stage && token.identity && !app.acquired && calls[6] == starts);
    } else {
        assert(err == ESP_OK && !token.identity && app.acquired && calls[6] == starts + 1);
        assert(s_radio.clients[ESP32_MQUICKJS_WIFI_RADIO_CLIENT_APPLICATION] == 1);
    }
}

static void test_lifecycle_exhaustion(void)
{
    esp32_mquickjs_wifi_radio_lifecycle_t token = {0};
    s_radio.next_lifecycle_identity = UINT32_MAX;
    assert(esp32_mquickjs_wifi_radio_begin_lifecycle(NULL, NULL, NULL, &token) == ESP_OK);
    assert(token.identity == UINT32_MAX);
    assert(esp32_mquickjs_wifi_radio_finish_lifecycle(&token, true) == ESP_OK);
    assert(esp32_mquickjs_wifi_radio_begin_lifecycle(NULL, NULL, NULL, &token) == ESP_ERR_NO_MEM);
}

static void *channel_callback_thread(void *opaque)
{
    (void)opaque;pause_channel_read = true;
    wifi_radio_channel_event(NULL, WIFI_EVENT, WIFI_EVENT_HOME_CHANNEL_CHANGE, NULL);
    return NULL;
}
static void test_shutdown_callback_barrier(void)
{
    esp32_mquickjs_wifi_radio_lease_t sta = acquire_sta();
    esp32_mquickjs_wifi_radio_release(&sta);
    pause_at = 15;
    pthread_t callback, closing;
    pthread_create(&callback, NULL, channel_callback_thread, NULL);
    pthread_mutex_lock(&schedule_lock);
    while (!paused) pthread_cond_wait(&schedule_cond, &schedule_lock);
    pthread_mutex_unlock(&schedule_lock);
    pthread_create(&closing, NULL, shutdown_thread, NULL);
    pthread_mutex_lock(&schedule_lock);
    while (!unregistered) pthread_cond_wait(&schedule_cond, &schedule_lock);
    assert(!calls[10] && atomic_load(&s_channel_callbacks) == 1);
    resume_driver = 1;pthread_cond_broadcast(&schedule_cond);
    pthread_mutex_unlock(&schedule_lock);
    pthread_join(callback, NULL);pthread_join(closing, NULL);
    assert(calls[10] == 1 && !s_channel_event_instance && !atomic_load(&s_channel_callbacks));
}

static esp32_mquickjs_wifi_radio_lifecycle_t restart_token;
static void *restart_thread(void *opaque)
{
    assert(esp32_mquickjs_wifi_radio_restart_lifecycle(&restart_token, WIFI_MODE_STA, opaque) == ESP_OK);
    return NULL;
}
static void test_restart_acquire_barrier(void)
{
    esp32_mquickjs_wifi_radio_lease_t app = {0}, other = {0};
    assert(esp32_mquickjs_wifi_radio_begin_lifecycle(NULL, NULL, NULL, &restart_token) == ESP_OK);
    pause_at = 2;
    pthread_t restarting, acquiring;
    pthread_create(&restarting, NULL, restart_thread, &app);
    pthread_mutex_lock(&schedule_lock);
    while (!paused) pthread_cond_wait(&schedule_cond, &schedule_lock);
    pthread_mutex_unlock(&schedule_lock);
    pthread_create(&acquiring, NULL, acquire_thread, &other);
    pthread_mutex_lock(&schedule_lock);
    while (!mutex_contended) pthread_cond_wait(&schedule_cond, &schedule_lock);
    assert(!calls[6]);resume_driver = 1;pthread_cond_broadcast(&schedule_cond);
    pthread_mutex_unlock(&schedule_lock);
    pthread_join(restarting, NULL);pthread_join(acquiring, NULL);
    assert(app.acquired && other.acquired && calls[6] == 1 && !restart_token.identity);
}

static void test_application_settings_owner(void)
{
    esp32_mquickjs_wifi_radio_lease_t sta = acquire_sta(), app = {0};
    assert(esp32_mquickjs_wifi_radio_acquire(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_APPLICATION, WIFI_MODE_STA, &app) == ESP_OK);
    wifi_ps_type_t actual;
    assert(esp32_mquickjs_wifi_radio_set_power_save(&sta, WIFI_PS_NONE, &actual) == ESP_OK);
    esp32_mquickjs_wifi_radio_lease_t other = acquire();
    assert(esp32_mquickjs_wifi_radio_set_power_save(&sta, WIFI_PS_NONE, &actual) == ESP_ERR_INVALID_STATE);
    esp32_mquickjs_wifi_radio_release(&other);
    esp32_mquickjs_wifi_radio_lifecycle_t token = {0};
    assert(esp32_mquickjs_wifi_radio_begin_lifecycle(&app, &sta, NULL, &token) == ESP_OK);
    assert(esp32_mquickjs_wifi_radio_set_power_save(&sta, WIFI_PS_NONE, &actual) == ESP_ERR_INVALID_STATE);
    assert(calls[11] == 1);
}

static wifi_config_t ap_config(void)
{
    wifi_config_t c = {.ap = {.beacon_interval=100,.dtim_period=1,.csa_count=3,.pairwise_cipher=WIFI_CIPHER_TYPE_CCMP}};
    memcpy(c.ap.ssid, "test-ap", 7);c.ap.ssid_len = 7;c.ap.channel = 6;
    c.ap.max_connection = 4;c.ap.authmode = WIFI_AUTH_WPA2_PSK;
    memcpy(c.ap.password, "12345678", 8);return c;
}
/* Public AP activation uses the shared configuration coordinator. Its SDK
 * mutation/replay cases are in the production configure/AP Python fixtures;
 * this ESP-NOW-only Radio fixture exercises native AP validation without JS. */
static void test_ap_validation(void)
{
    wifi_config_t c = ap_config();
    int before[21];memcpy(before, calls, sizeof(calls));
    assert(esp32_mquickjs_wifi_radio_validate_ap_config(NULL)==ESP_ERR_INVALID_ARG);
    assert(esp32_mquickjs_wifi_radio_validate_ap_config(&c)==ESP_OK);
    c.ap.channel=0;assert(esp32_mquickjs_wifi_radio_validate_ap_config(&c)==ESP_OK); /* Automatic selection. */
    c.ap.channel=15;assert(esp32_mquickjs_wifi_radio_validate_ap_config(&c)==ESP_ERR_INVALID_ARG);
    c=ap_config();c.ap.ssid_len=0;assert(esp32_mquickjs_wifi_radio_validate_ap_config(&c)==ESP_ERR_INVALID_ARG);
    c=ap_config();c.ap.beacon_interval=101;assert(esp32_mquickjs_wifi_radio_validate_ap_config(&c)==ESP_ERR_INVALID_ARG);
    c=ap_config();c.ap.dtim_period=0;assert(esp32_mquickjs_wifi_radio_validate_ap_config(&c)==ESP_ERR_INVALID_ARG);
    c=ap_config();c.ap.csa_count=0;assert(esp32_mquickjs_wifi_radio_validate_ap_config(&c)==ESP_ERR_INVALID_ARG);
    c=ap_config();c.ap.max_connection=5;assert(esp32_mquickjs_wifi_radio_validate_ap_config(&c)==ESP_ERR_INVALID_ARG);
    c=ap_config();c.ap.password[7]=0;assert(esp32_mquickjs_wifi_radio_validate_ap_config(&c)==ESP_ERR_INVALID_ARG);
    c=ap_config();c.ap.authmode=WIFI_AUTH_OPEN;assert(esp32_mquickjs_wifi_radio_validate_ap_config(&c)==ESP_ERR_INVALID_ARG);
    memset(c.ap.password,0,sizeof(c.ap.password));assert(esp32_mquickjs_wifi_radio_validate_ap_config(&c)==ESP_OK);
    c.ap.pmf_cfg.required=true;assert(esp32_mquickjs_wifi_radio_validate_ap_config(&c)==ESP_ERR_INVALID_ARG);
    assert(!memcmp(before,calls,sizeof(calls)) && !s_radio.lifecycle.identity && !s_radio.driver_owned);
}

static esp32_mquickjs_wifi_radio_lifecycle_t prepare_handoff(void)
{
    esp32_mquickjs_wifi_radio_lease_t sta = acquire_sta();
    esp32_mquickjs_wifi_radio_lifecycle_t token = {0};
    assert(esp32_mquickjs_wifi_radio_begin_lifecycle(NULL, &sta, NULL, &token) == ESP_OK);
    esp32_mquickjs_wifi_radio_release(&sta);
    assert(esp32_mquickjs_wifi_radio_quiesce_lifecycle(&token) == ESP_OK);
    return token;
}

static void test_lifecycle_handoff(void)
{
    esp32_mquickjs_wifi_radio_lifecycle_t token = prepare_handoff(), stale = token;
    esp32_mquickjs_wifi_radio_lease_t app = {0}, sta = {0};
    int starts = calls[6], inits = calls[2];
    /* Aliased outputs are invalid before driver calls or identity consumption. */
    uint32_t next_identity = s_radio.next_lease_identity;
    assert(esp32_mquickjs_wifi_radio_resume_lifecycle(&token, WIFI_MODE_STA, true,
        &app, &app, NULL, NULL) == ESP_ERR_INVALID_ARG);
    assert(token.identity && calls[6] == starts && s_radio.next_lease_identity == next_identity);
    assert(esp32_mquickjs_wifi_radio_resume_lifecycle(&token, WIFI_MODE_STA, true,
        &app, &sta, NULL, NULL) == ESP_OK);
    assert(!token.identity && app.acquired && sta.acquired && app.identity != sta.identity);
    assert(calls[6] == starts + 1 && calls[2] == inits);
    assert(s_radio.clients[ESP32_MQUICKJS_WIFI_RADIO_CLIENT_APPLICATION] == 1 &&
           s_radio.clients[ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_STA] == 1);
    assert(esp32_mquickjs_wifi_radio_begin_lifecycle(&app, &sta, NULL, &token) == ESP_OK);
    esp32_mquickjs_wifi_radio_release(&app);
    esp32_mquickjs_wifi_radio_release(&sta);
    assert(esp32_mquickjs_wifi_radio_quiesce_lifecycle(&token) == ESP_OK);
    assert(esp32_mquickjs_wifi_radio_resume_lifecycle(&stale, WIFI_MODE_STA, false,
        NULL, NULL, NULL, NULL) == ESP_ERR_INVALID_STATE);
    assert(s_radio.lifecycle.identity == token.identity);
    assert(esp32_mquickjs_wifi_radio_resume_lifecycle(&token, WIFI_MODE_STA, false,
        NULL, NULL, NULL, NULL) == ESP_OK);
    assert(!token.identity && !s_radio.started && s_radio.driver_owned && calls[6] == starts + 1);
}

static void test_handoff_identity_capacity(void)
{
    esp32_mquickjs_wifi_radio_lifecycle_t token = prepare_handoff();
    esp32_mquickjs_wifi_radio_lease_t app = {0}, sta = {0};
    s_radio.next_lease_identity = UINT32_MAX;
    int starts = calls[6];
    assert(esp32_mquickjs_wifi_radio_resume_lifecycle(&token, WIFI_MODE_STA, true,
        &app, &sta, NULL, NULL) == ESP_ERR_NO_MEM);
    assert(token.identity && !app.acquired && !sta.acquired && calls[6] == starts);
    assert(s_radio.next_lease_identity == UINT32_MAX);
    assert(esp32_mquickjs_wifi_radio_resume_lifecycle(&token, WIFI_MODE_STA, true,
        &app, NULL, NULL, NULL) == ESP_OK);
    assert(app.identity == UINT32_MAX && !token.identity && s_radio.next_lease_identity == 0);
}

static void test_handoff_readback_failure(void)
{
    esp32_mquickjs_wifi_radio_lifecycle_t token = prepare_handoff();
    esp32_mquickjs_wifi_radio_lease_t app = {0}, sta = {0}, foreign = {0};
    mode_error_after_start = true;
    assert(esp32_mquickjs_wifi_radio_resume_lifecycle(&token, WIFI_MODE_STA, true,
        &app, &sta, NULL, NULL) == -88);
    assert(token.identity && !app.acquired && !sta.acquired && s_radio.stop_required);
    for (size_t i = 0; i < WIFI_RADIO_MAX_LEASES; ++i) assert(!s_radio.leases[i].identity);
    assert(!strcmp(s_radio.fault_stage, "resume-mode-readback") && s_radio.fault_error == -88);
    assert(esp32_mquickjs_wifi_radio_acquire(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_ESPNOW,
        WIFI_MODE_STA, &foreign) == ESP_ERR_INVALID_STATE);
    mode_error_after_start = false;
    assert(esp32_mquickjs_wifi_radio_finish_lifecycle(&token, true) == ESP_OK);
    assert(!token.identity && !s_radio.driver_owned);
}

static void test_activation_controls(void)
{
    for(int scenario=0;scenario<=8;scenario++) {
        esp32_mquickjs_wifi_radio_lifecycle_t token=prepare_handoff();
        esp32_mquickjs_wifi_radio_lease_t app={0},sta={0},foreign={0};
        esp32_mquickjs_wifi_radio_start_controls_t controls={.tx_power_set=true,.tx_power_quarter_dbm=80};
        int starts=calls[6];uint32_t next=s_radio.next_lease_identity;
        controls.tx_power_quarter_dbm=7;
        assert(esp32_mquickjs_wifi_radio_resume_lifecycle(&token,WIFI_MODE_STA,true,&app,&sta,NULL,&controls)==ESP_ERR_INVALID_ARG);
        assert(calls[6]==starts && s_radio.next_lease_identity==next && token.identity);
        controls.tx_power_quarter_dbm=80;
        assert(esp32_mquickjs_wifi_radio_resume_lifecycle(&token,WIFI_MODE_STA,false,NULL,NULL,NULL,&controls)==ESP_ERR_INVALID_ARG);
        assert(calls[6]==starts && s_radio.next_lease_identity==next);
        activation_case=scenario;activation_calls=0;driver_power=72;activation_test=true;
        int err=esp32_mquickjs_wifi_radio_resume_lifecycle(&token,WIFI_MODE_STA,true,&app,&sta,NULL,&controls);
        activation_test=false;
        assert(calls[6]==starts+1 && s_radio.started);
        assert(!s_radio.activation.persistent_mutation_possible);
        if(!scenario) {
            assert(err==ESP_OK && driver_power==78 && activation_calls==3 && !token.identity && app.acquired && sta.acquired);
            assert(!strcmp(s_radio.activation.stage,"complete") && !s_radio.activation.rollback_attempted);
            assert(esp32_mquickjs_wifi_radio_begin_lifecycle(&app,&sta,NULL,&token)==ESP_OK);
            esp32_mquickjs_wifi_radio_release(&app);esp32_mquickjs_wifi_radio_release(&sta);
        } else {
            assert(err==(scenario<=3?-77:ESP_ERR_INVALID_RESPONSE));
            assert(token.identity && !app.acquired && !sta.acquired && s_radio.stop_required);
            assert(s_radio.fault_error==err && s_radio.activation.error==err);
            for(size_t i=0;i<WIFI_RADIO_MAX_LEASES;i++)assert(!s_radio.leases[i].identity);
            assert(esp32_mquickjs_wifi_radio_acquire(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_ESPNOW,WIFI_MODE_STA,&foreign)==ESP_ERR_INVALID_STATE);
            if(scenario==1 || scenario==8)assert(!s_radio.activation.mutation_attempted && !s_radio.activation.rollback_attempted && driver_power==72);
            else if(scenario<5)assert(s_radio.activation.rollback_complete && driver_power==72);
            else {
                assert(!s_radio.activation.rollback_complete && !strcmp(s_radio.cleanup_stage,"activation-rollback"));
                assert(s_radio.activation.rollback_error==(scenario==7?ESP_ERR_INVALID_RESPONSE:-88));
            }
        }
        assert(esp32_mquickjs_wifi_radio_finish_lifecycle(&token,true)==ESP_OK);
        assert(!token.identity && !s_radio.driver_owned);
    }
}

static void prepare_apsta_owners(esp32_mquickjs_wifi_radio_lease_t *app,
    esp32_mquickjs_wifi_radio_lease_t *sta,esp32_mquickjs_wifi_radio_lease_t *ap)
{
    wifi_radio_operation_lock();
    assert(wifi_radio_acquire_locked(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_APPLICATION,WIFI_MODE_APSTA,app)==ESP_OK);
    assert(wifi_radio_acquire_locked(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_STA,WIFI_MODE_STA,sta)==ESP_OK);
    assert(wifi_radio_acquire_locked(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_AP,WIFI_MODE_AP,ap)==ESP_OK);
    wifi_radio_operation_unlock();
    assert(esp32_mquickjs_wifi_radio_ensure_started(app)==ESP_OK && s_radio.event_live==WIFI_MODE_APSTA);
}

static void test_partial_ap_stop_wait_budget(void)
{
    esp32_mquickjs_wifi_radio_lease_t app={0},sta={0},ap={0};
    prepare_apsta_owners(&app,&sta,&ap);
    unsigned app_identity=app.identity,sta_identity=sta.identity;
    int stops=calls[9],modes=calls[5];
    esp32_mquickjs_wifi_radio_lifecycle_t token={0};
    assert(esp32_mquickjs_wifi_wait_begin(10)==ESP_OK);
    radio_test_ticks+=8;
    assert(esp32_mquickjs_wifi_radio_begin_ap_stop(&app,&sta,&ap,&token)==ESP_OK);
    ap_mode_suppress_stop=true;
    TickType_t began=radio_test_ticks;
    assert(esp32_mquickjs_wifi_radio_quiesce_ap_lifecycle(&token)==ESP_ERR_TIMEOUT);
    assert((TickType_t)(radio_test_ticks-began)==2 && calls[5]==modes+1 && calls[9]==stops);
    assert(app.acquired && sta.acquired && ap.acquired && token.identity);
    esp32_mquickjs_wifi_wait_end();
    ap_mode_suppress_stop=false;driver_lifecycle_events(WIFI_MODE_AP,false);
    assert(esp32_mquickjs_wifi_wait_begin(10)==ESP_OK);
    assert(esp32_mquickjs_wifi_radio_quiesce_ap_lifecycle(&token)==ESP_OK);
    assert(esp32_mquickjs_wifi_radio_finish_ap_stop(&token,&ap)==ESP_OK);
    esp32_mquickjs_wifi_wait_end();
    assert(!ap.acquired && app.identity==app_identity && sta.identity==sta_identity);
    assert(s_radio.event_live==WIFI_MODE_STA && s_radio.started && calls[5]==modes+1 && calls[9]==stops);
    esp32_mquickjs_wifi_radio_release(&app);esp32_mquickjs_wifi_radio_release(&sta);
    assert(esp32_mquickjs_wifi_radio_shutdown()==ESP_OK);
}

static void test_partial_ap_stop(void)
{
    for(int scenario=0;scenario<8;scenario++) {
        ap_mode_changed=false;
        esp32_mquickjs_wifi_radio_lease_t app={0},sta={0},ap={0},foreign={0};
        prepare_apsta_owners(&app,&sta,&ap);
        unsigned app_identity=app.identity,sta_identity=sta.identity;
        esp32_mquickjs_wifi_radio_lifecycle_t token={0};
        int starts=calls[6],stops=calls[9],deinits=calls[10],modes=calls[5];
        assert(esp32_mquickjs_wifi_radio_begin_ap_stop(&app,&ap,&sta,&token)==ESP_ERR_INVALID_STATE && !token.identity);
        size_t station_slot=0;while(s_radio.leases[station_slot].identity!=sta.identity)station_slot++;
        s_radio.leases[station_slot].required_mode=WIFI_MODE_APSTA;
        assert(esp32_mquickjs_wifi_radio_begin_ap_stop(&app,&sta,&ap,&token)==ESP_ERR_INVALID_STATE && !token.identity);
        s_radio.leases[station_slot].required_mode=WIFI_MODE_STA;
        assert(esp32_mquickjs_wifi_radio_begin_ap_stop(&app,&sta,&ap,&token)==ESP_OK);
        assert(app.acquired && sta.acquired && ap.acquired && calls[5]==modes);
        esp32_mquickjs_wifi_radio_lifecycle_t stale=token;stale.identity++;
        assert(esp32_mquickjs_wifi_radio_quiesce_ap_lifecycle(&stale)==ESP_ERR_INVALID_STATE && calls[5]==modes);
        assert(esp32_mquickjs_wifi_radio_acquire(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_ESPNOW,WIFI_MODE_STA,&foreign)==ESP_ERR_INVALID_STATE);
        assert(esp32_mquickjs_wifi_radio_finish_ap_stop(&token,&ap)==ESP_ERR_INVALID_STATE && ap.acquired);
        if(scenario==1)ap_mode_suppress_stop=true;
        if(scenario==2)fence_error=ESP_ERR_TIMEOUT;
        if(scenario==3)ap_mode_partial_error=true;
        if(scenario==4)fail_at=5;
        if(scenario==5)mode_error_after_start=true;
        if(scenario==6)ap_mode_readback_error=true;
        if(scenario==7)driver_lifecycle_events(WIFI_MODE_STA,false);
        int err=esp32_mquickjs_wifi_radio_quiesce_ap_lifecycle(&token);
        if(scenario==0)assert(err==ESP_OK);
        else {
            assert(err!=ESP_OK && token.identity && ap.acquired && sta.acquired && s_radio.started);
            if(scenario==1 || scenario==2 || scenario==7)assert(err==ESP_ERR_TIMEOUT);
            if(scenario==3 || scenario==5)assert(err==-88);
            if(scenario==4)assert(err==0x705);
            if(scenario==6)assert(err==-99 && s_radio.ap_stop_phase==AP_STOP_EVENTS_DONE);
            if(scenario==1) {ap_mode_suppress_stop=false;driver_lifecycle_events(WIFI_MODE_AP,false);}
            if(scenario==2)fence_error=0;
            if(scenario==3)ap_mode_partial_error=false;
            if(scenario==5)mode_error_after_start=false;
            if(scenario==6)ap_mode_readback_error=false;
            if(scenario==4 || scenario==7) {
                fail_at=0;
                assert(esp32_mquickjs_wifi_radio_quiesce_ap_lifecycle(&token)==(scenario==4?0x705:ESP_ERR_TIMEOUT) && calls[5]==modes+1);
                /* Explicit whole cleanup adopts the SAME reservation before owners exit. */
                assert(esp32_mquickjs_wifi_radio_adopt_ap_stop(&stale)==ESP_ERR_INVALID_STATE);
                const char *fault=s_radio.fault_stage;
                unsigned identity=token.identity;
                assert(esp32_mquickjs_wifi_radio_adopt_ap_stop(&token)==ESP_OK);
                assert(token.identity==identity && s_radio.fault_stage==fault && s_radio.started);
                assert(app.acquired && sta.acquired && ap.acquired && s_radio.ap_stop_phase==AP_STOP_IDLE);
                assert(esp32_mquickjs_wifi_radio_adopt_ap_stop(&token)==ESP_ERR_INVALID_STATE);
                assert(esp32_mquickjs_wifi_radio_quiesce_ap_lifecycle(&token)==ESP_ERR_INVALID_STATE);
                assert(calls[9]==stops && calls[10]==deinits && calls[5]==modes+1);
                esp32_mquickjs_wifi_radio_release(&ap);esp32_mquickjs_wifi_radio_release(&sta);esp32_mquickjs_wifi_radio_release(&app);
                assert(esp32_mquickjs_wifi_radio_finish_lifecycle(&token,true)==ESP_OK && !token.identity);
                continue;
            }
            assert(esp32_mquickjs_wifi_radio_quiesce_ap_lifecycle(&token)==ESP_OK);
        }
        assert(calls[5]==modes+1 && calls[6]==starts && calls[9]==stops && calls[10]==deinits);
        assert(esp32_mquickjs_wifi_radio_check_ap_stopped_lifecycle(&token)==ESP_OK);
        assert(s_radio.event_live==WIFI_MODE_STA && s_radio.effective_mode==WIFI_MODE_STA);
        /* Netif retirement happens at this boundary in the later AP adapter. */
        assert(esp32_mquickjs_wifi_radio_finish_ap_stop(&token,&ap)==ESP_OK && !token.identity && !ap.acquired);
        assert(app.acquired && sta.acquired && app.identity==app_identity && sta.identity==sta_identity);
        assert(wifi_radio_requested_mode()==WIFI_MODE_STA && s_radio.started && s_radio.stop_required);
        assert(!s_radio.fault_stage && !s_radio.cleanup_stage && s_radio.ap_stop_phase==AP_STOP_IDLE);
        assert(esp32_mquickjs_wifi_radio_ensure_started(&app)==ESP_OK && calls[5]==modes+1 && calls[6]==starts);
        esp32_mquickjs_wifi_radio_release(&sta);esp32_mquickjs_wifi_radio_release(&app);
        assert(esp32_mquickjs_wifi_radio_shutdown()==ESP_OK);
    }
}

/* The feature-disabled Radio fixture stages the already admitted reservation;
 * real config validation/matching is covered by the VM AP-config fixture.
 * These cases exercise production activation, event fencing and abort helpers. */
static void reopen_readback_failure(void) { mode_error_after_start=true; }

static void test_ap_reopen_activation(void)
{
    for (int scenario=0;scenario<12;scenario++) {
        esp32_mquickjs_wifi_radio_lease_t app={0},sta={0},ap={0};
        prepare_apsta_owners(&app,&sta,&ap);
        esp32_mquickjs_wifi_radio_lifecycle_t token={0};
        assert(esp32_mquickjs_wifi_radio_begin_ap_stop(&app,&sta,&ap,&token)==ESP_OK);
        assert(esp32_mquickjs_wifi_radio_quiesce_ap_lifecycle(&token)==ESP_OK);
        assert(esp32_mquickjs_wifi_radio_finish_ap_stop(&token,&ap)==ESP_OK);
        unsigned app_id=app.identity,sta_id=sta.identity;
        wifi_radio_operation_lock();
        assert(wifi_radio_begin_lifecycle_locked(&app,&sta,NULL,&token)==ESP_OK);
        assert(wifi_radio_acquire_locked(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_AP,WIFI_MODE_AP,&ap)==ESP_OK);
        s_radio.ap_transition_application=app.identity;s_radio.ap_transition_station=sta.identity;
        s_radio.ap_transition_access_point=ap.identity;s_radio.ap_reopen_pending=true;
        wifi_radio_operation_unlock();
        uint8_t primary=0;
        int modes=calls[5],starts=calls[6],stops=calls[9],configs=calls[16];
        esp32_mquickjs_wifi_radio_lifecycle_t stale=token;stale.identity++;
        assert(esp32_mquickjs_wifi_radio_check_ap_reopen(&stale)==ESP_ERR_INVALID_STATE);
        assert(esp32_mquickjs_wifi_radio_finish_ap_reopen(&stale,&primary)==ESP_ERR_INVALID_STATE && calls[5]==modes);
        assert(esp32_mquickjs_wifi_radio_check_ap_reopen(&token)==ESP_OK);
        if(scenario==1) {
            /* AP netif setup failed before driver mode was attempted. */
            assert(esp32_mquickjs_wifi_radio_abort_ap_reopen(&token)==ESP_OK);
            assert(esp32_mquickjs_wifi_radio_check_ap_stopped_lifecycle(&token)==ESP_OK);
            assert(esp32_mquickjs_wifi_radio_finish_ap_stop(&token,&ap)==ESP_OK);
            assert(calls[5]==modes && app.identity==app_id && sta.identity==sta_id);
        } else if (scenario>=9) {
            /* Native pre-start helper confirmed rejection before AP start and
             * the SDK's AP allocation rollback. Its separate production
             * fixture covers that proof; exercise actual Radio cleanup here. */
            s_radio.ap_reopen_attempted=s_radio.ap_reopen_quiesced=true;
            s_radio.ap_reopen_restore_complete=scenario!=10;
            wifi_radio_cleanup_fault("ap-prestart-config",-77);
            assert(esp32_mquickjs_wifi_radio_abort_ap_reopen(&stale)==ESP_ERR_INVALID_STATE);
            assert(esp32_mquickjs_wifi_radio_abort_ap_reopen(&token)==ESP_OK);
            assert(s_radio.event_phase==RADIO_EVENTS_AP_STOP && !s_radio.event_expected);
            if(scenario==11) {
                fence_error=ESP_ERR_TIMEOUT;
                assert(esp32_mquickjs_wifi_radio_quiesce_ap_lifecycle(&token)==ESP_ERR_TIMEOUT);
                assert(token.identity && ap.acquired && s_radio.ap_stop_phase==AP_STOP_SUBMITTED);
                fence_error=0;
            }
            assert(esp32_mquickjs_wifi_radio_quiesce_ap_lifecycle(&token)==ESP_OK);
            assert(esp32_mquickjs_wifi_radio_finish_ap_stop(&token,&ap)==ESP_OK);
            assert(!token.identity && !ap.acquired && wifi_radio_lease_valid(&app) && wifi_radio_lease_valid(&sta));
            assert((s_radio.fault_stage!=NULL)==(scenario==10));
            assert(s_radio.driver_state==(scenario==10?ESP32_MQUICKJS_WIFI_RADIO_FAULTED:ESP32_MQUICKJS_WIFI_RADIO_STARTED));
            assert(calls[5]==modes && calls[6]==starts && calls[9]==stops && calls[16]==configs);
            assert(app.identity==app_id && sta.identity==sta_id && s_radio.event_live==WIFI_MODE_STA);
        } else {
            if(scenario==2)suppress_start_events=true;
            if(scenario==3)fence_error=ESP_ERR_TIMEOUT;
            if(scenario==4)ap_mode_partial_error=true;
            if(scenario==5)fail_at=5;
            if(scenario==6)driver_lifecycle_events(WIFI_MODE_STA,false);
            if(scenario==7)on_event_delay=reopen_readback_failure;
            if(scenario==8)channel_read_error=-77;
            int err=esp32_mquickjs_wifi_radio_finish_ap_reopen(&token,&primary);
            if(!scenario) {
                assert(err==ESP_OK && !token.identity && s_radio.event_live==WIFI_MODE_APSTA);
                assert(s_radio.effective_mode==WIFI_MODE_APSTA && wifi_radio_requested_mode()==WIFI_MODE_APSTA);
            } else {
                assert(err!=ESP_OK && token.identity && s_radio.ap_reopen_pending && app.acquired && sta.acquired && ap.acquired);
                int once=calls[5];
                assert(esp32_mquickjs_wifi_radio_finish_ap_reopen(&token,&primary)==ESP_ERR_INVALID_STATE && calls[5]==once);
                assert(esp32_mquickjs_wifi_radio_abort_ap_reopen(&stale)==ESP_ERR_INVALID_STATE);
                assert(esp32_mquickjs_wifi_radio_abort_ap_reopen(&token)==ESP_OK && !s_radio.ap_reopen_pending);
                assert(esp32_mquickjs_wifi_radio_abort_ap_reopen(&token)==ESP_ERR_INVALID_STATE);
                suppress_start_events=false;fence_error=0;ap_mode_partial_error=false;fail_at=0;
                on_event_delay=NULL;mode_error_after_start=false;channel_read_error=0;
                if(scenario==2)driver_lifecycle_events(WIFI_MODE_AP,true);
                /* Explicit whole cleanup retains the activation fault until shutdown. */
                const char *fault=s_radio.fault_stage;
                assert(esp32_mquickjs_wifi_radio_adopt_ap_stop(&token)==ESP_OK && s_radio.fault_stage==fault);
            }
            assert(calls[6]==starts && calls[9]==stops && calls[16]==configs);
            assert(app.identity==app_id && sta.identity==sta_id);
        }
        esp32_mquickjs_wifi_radio_release(&ap);esp32_mquickjs_wifi_radio_release(&sta);esp32_mquickjs_wifi_radio_release(&app);
        if(token.identity)assert(esp32_mquickjs_wifi_radio_finish_lifecycle(&token,true)==ESP_OK);
        else assert(esp32_mquickjs_wifi_radio_shutdown()==ESP_OK);
    }
}

static void test_lifecycle_owner_roles(void)
{
    esp32_mquickjs_wifi_radio_lease_t app = {0}, sta = {0}, ap = {0};
    esp32_mquickjs_wifi_radio_lifecycle_t token = {0};
    /* Build the same exact registry entries as resume, without pretending the
     * host SDK stub started APSTA or testing a separate lifecycle model. */
    wifi_radio_operation_lock();
    assert(wifi_radio_acquire_locked(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_APPLICATION, WIFI_MODE_APSTA, &app) == ESP_OK);
    assert(wifi_radio_acquire_locked(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_STA, WIFI_MODE_STA, &sta) == ESP_OK);
    assert(wifi_radio_acquire_locked(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_AP, WIFI_MODE_AP, &ap) == ESP_OK);
    wifi_radio_operation_unlock();
    assert(esp32_mquickjs_wifi_radio_begin_lifecycle(&app, &ap, &sta, &token) == ESP_ERR_INVALID_STATE);
    assert(!token.identity);
    assert(esp32_mquickjs_wifi_radio_begin_lifecycle(&app, &sta, NULL, &token) == ESP_ERR_INVALID_STATE);
    assert(esp32_mquickjs_wifi_radio_begin_lifecycle(&app, &sta, &ap, &token) == ESP_OK);
    esp32_mquickjs_wifi_radio_release(&ap);
    esp32_mquickjs_wifi_radio_release(&sta);
    esp32_mquickjs_wifi_radio_release(&app);
    assert(esp32_mquickjs_wifi_radio_finish_lifecycle(&token, false) == ESP_OK);
}

static void test_helper_lifecycle_admission(void)
{
    esp32_mquickjs_wifi_radio_lease_t sta = acquire_sta();
    esp32_mquickjs_wifi_radio_lifecycle_t token = {0};
    assert(esp32_mquickjs_wifi_radio_begin_lifecycle(NULL, &sta, NULL, &token) == ESP_OK);
    assert(esp32_mquickjs_wifi_radio_check_stopped_lifecycle(&token, false) == ESP_ERR_INVALID_STATE);
    esp32_mquickjs_wifi_radio_release(&sta);
    assert(esp32_mquickjs_wifi_radio_quiesce_lifecycle(&token) == ESP_OK);
    int before[21];memcpy(before,calls,sizeof(calls));
    assert(esp32_mquickjs_wifi_radio_check_stopped_lifecycle(&token, false) == ESP_OK);
    esp32_mquickjs_wifi_radio_lifecycle_t stale = token;stale.identity++;
    assert(esp32_mquickjs_wifi_radio_check_stopped_lifecycle(&stale, true) == ESP_ERR_INVALID_STATE);
    (void)wifi_radio_record_fault("injected-config", -9);
    assert(esp32_mquickjs_wifi_radio_check_stopped_lifecycle(&token, false) == ESP_ERR_INVALID_STATE);
    assert(esp32_mquickjs_wifi_radio_check_stopped_lifecycle(&token, true) == ESP_OK);
    assert(!memcmp(before,calls,sizeof(calls)));
    assert(esp32_mquickjs_wifi_radio_finish_lifecycle(&token, true) == ESP_OK);
    assert(esp32_mquickjs_wifi_radio_begin_lifecycle(NULL,NULL,NULL,&token) == ESP_OK);
    esp32_mquickjs_wifi_radio_lease_t app = {0};fail_at=1;
    assert(esp32_mquickjs_wifi_radio_restart_lifecycle(&token,WIFI_MODE_STA,&app) == 0x701);
    assert(s_radio.restart_required);
    assert(esp32_mquickjs_wifi_radio_check_stopped_lifecycle(&token, true) == ESP_ERR_INVALID_STATE);
}

static void test_start_event_timeout(void)
{
    esp32_mquickjs_wifi_radio_lease_t lease = acquire();
    suppress_start_events = true;
    assert(esp32_mquickjs_wifi_radio_ensure_started(&lease) == ESP_ERR_TIMEOUT);
    assert(calls[6] == 1 && s_radio.started && s_radio.stop_required && !radio_test_wait_depth);
    assert(s_radio.event_phase == RADIO_EVENTS_START && !s_radio.event_seen && !fence_posts);
    assert(esp32_mquickjs_wifi_radio_ensure_started(&lease) == ESP_ERR_TIMEOUT && calls[6] == 1);
    driver_lifecycle_events(WIFI_MODE_STA, true); /* Late start cannot report a successful old call. */
    suppress_start_events = false;
    esp32_mquickjs_wifi_radio_release(&lease);
    assert(esp32_mquickjs_wifi_radio_shutdown() == ESP_OK && calls[9] == 1 && calls[10] == 1);
}

static void test_stop_event_suffix(bool queue_failure)
{
    esp32_mquickjs_wifi_radio_lease_t lease = acquire_sta();
    esp32_mquickjs_wifi_radio_release(&lease);
    if (queue_failure) fence_error = ESP_ERR_NO_MEM;
    else suppress_stop_events = true;
    int expected = queue_failure ? ESP_ERR_NO_MEM : ESP_ERR_TIMEOUT;
    assert(esp32_mquickjs_wifi_radio_stop() == expected);
    uint32_t identity = s_radio.event_identity;
    assert(s_radio.stop_submitted && s_radio.stop_required && calls[9] == 1 && !radio_test_wait_depth);
    assert(esp32_mquickjs_wifi_radio_stop() == expected && calls[9] == 1 && s_radio.event_identity == identity);
    fence_error = 0;suppress_stop_events = false;
    if (!queue_failure) driver_lifecycle_events(WIFI_MODE_STA, false);
    assert(esp32_mquickjs_wifi_radio_stop() == ESP_OK);
    assert(calls[9] == 1 && !s_radio.stop_submitted && !s_radio.stop_required && !s_radio.fault_stage);
    assert(esp32_mquickjs_wifi_radio_shutdown() == ESP_OK);
}

static void test_stop_wait_budget(void)
{
    esp32_mquickjs_wifi_radio_lease_t lease = acquire_sta();
    esp32_mquickjs_wifi_radio_release(&lease);
    suppress_stop_events = true;
    assert(esp32_mquickjs_wifi_wait_begin(10) == ESP_OK);
    radio_test_ticks += 7; /* Time consumed by an earlier cleanup step. */
    TickType_t started = radio_test_ticks;
    assert(esp32_mquickjs_wifi_radio_stop() == ESP_ERR_TIMEOUT);
    assert((TickType_t)(radio_test_ticks - started) == 3 && calls[9] == 1);
    uint32_t identity = s_radio.event_identity;
    assert(s_radio.stop_submitted && !radio_test_wait_depth);
    esp32_mquickjs_wifi_wait_end();
    driver_lifecycle_events(WIFI_MODE_STA, false);
    suppress_stop_events = false;
    assert(esp32_mquickjs_wifi_wait_begin(20) == ESP_OK);
    assert(esp32_mquickjs_wifi_radio_stop() == ESP_OK);
    assert(calls[9] == 1 && s_radio.event_identity == identity);
    esp32_mquickjs_wifi_wait_end();
    assert(esp32_mquickjs_wifi_radio_shutdown() == ESP_OK);
}

static void test_event_marker_revision(void)
{
    esp32_mquickjs_wifi_radio_lease_t lease = acquire_sta();
    esp32_mquickjs_wifi_radio_release(&lease);
    defer_fence = true;
    assert(esp32_mquickjs_wifi_radio_stop() == ESP_ERR_TIMEOUT);
    wifi_radio_event_fence_t stale = queued_fence;
    /* Another native event needs a marker after all of its default handlers. */
    driver_lifecycle_events(WIFI_MODE_STA, false);
    lifecycle_handler(NULL, ESP32QJS_WIFI_RADIO_CONTROL_EVENT, 1, &stale);
    assert(!s_radio.event_fence_seen && s_radio.event_revision != stale.revision);
    defer_fence = false;
    assert(esp32_mquickjs_wifi_radio_stop() == ESP_OK && calls[9] == 1);
    assert(esp32_mquickjs_wifi_radio_shutdown() == ESP_OK);
    lease = acquire();defer_fence = true;
    assert(esp32_mquickjs_wifi_radio_ensure_started(&lease) == ESP_ERR_TIMEOUT);
    lifecycle_handler(NULL, ESP32QJS_WIFI_RADIO_CONTROL_EVENT, 1, &stale);
    assert(!s_radio.event_fence_seen && s_radio.event_identity != stale.identity);
    defer_fence = false;esp32_mquickjs_wifi_radio_release(&lease);
    assert(esp32_mquickjs_wifi_radio_shutdown() == ESP_OK);
}

static void test_event_apsta_stop(void)
{
    esp32_mquickjs_wifi_radio_lease_t app = {0};
    assert(esp32_mquickjs_wifi_radio_acquire(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_APPLICATION,
        WIFI_MODE_APSTA, &app) == ESP_OK);
    assert(esp32_mquickjs_wifi_radio_ensure_started(&app) == ESP_OK);
    assert(s_radio.event_seen == WIFI_MODE_APSTA && s_radio.event_live == WIFI_MODE_APSTA);
    esp32_mquickjs_wifi_radio_release(&app);suppress_stop_events = true;
    assert(esp32_mquickjs_wifi_radio_stop() == ESP_ERR_TIMEOUT);
    driver_lifecycle_events(WIFI_MODE_AP, false);
    assert(esp32_mquickjs_wifi_radio_stop() == ESP_ERR_TIMEOUT && calls[9] == 1);
    assert(s_radio.event_seen == WIFI_MODE_AP && s_radio.event_live == WIFI_MODE_STA);
    driver_lifecycle_events(WIFI_MODE_STA, false);
    assert(esp32_mquickjs_wifi_radio_stop() == ESP_OK && calls[9] == 1 && !s_radio.event_live);
    assert(esp32_mquickjs_wifi_radio_shutdown() == ESP_OK);
}

static void test_event_interrupt(void)
{
    esp32_mquickjs_wifi_radio_lease_t lease = acquire();
    radio_test_interrupt = true;
    assert(esp32_mquickjs_wifi_radio_ensure_started(&lease) == ESP_ERR_INVALID_STATE);
    assert(s_radio.stop_required && !radio_test_wait_depth && calls[6] == 1 && !fence_posts);
    radio_test_interrupt = false;esp32_mquickjs_wifi_radio_release(&lease);
    assert(esp32_mquickjs_wifi_radio_shutdown() == ESP_OK && !radio_test_wait_depth);
}

static void test_event_exhaustion(void)
{
    esp32_mquickjs_wifi_radio_lease_t lease = acquire();
    s_radio.event_identity = UINT32_MAX;
    assert(esp32_mquickjs_wifi_radio_ensure_started(&lease) == ESP_ERR_NO_MEM);
    assert(!calls[6] && s_radio.restart_required && s_radio.event_identity == UINT32_MAX);
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    if (!strcmp(argv[1], "start-event-timeout")) test_start_event_timeout();
    else if (!strcmp(argv[1], "stop-wait-budget")) test_stop_wait_budget();
    else if (!strcmp(argv[1], "stop-event-timeout")) test_stop_event_suffix(false);
    else if (!strcmp(argv[1], "stop-event-queue")) test_stop_event_suffix(true);
    else if (!strcmp(argv[1], "event-marker-revision")) test_event_marker_revision();
    else if (!strcmp(argv[1], "event-apsta-stop")) test_event_apsta_stop();
    else if (!strcmp(argv[1], "event-interrupt")) test_event_interrupt();
    else if (!strcmp(argv[1], "event-exhaustion")) test_event_exhaustion();
    else if (!strcmp(argv[1], "helper-lifecycle-admission")) test_helper_lifecycle_admission();
    else if (!strcmp(argv[1], "lifecycle-handoff")) test_lifecycle_handoff();
    else if (!strcmp(argv[1], "handoff-capacity")) test_handoff_identity_capacity();
    else if (!strcmp(argv[1], "handoff-readback")) test_handoff_readback_failure();
    else if (!strcmp(argv[1], "activation-controls")) test_activation_controls();
    else if (!strcmp(argv[1], "ap-reopen-activation")) test_ap_reopen_activation();
    else if (!strcmp(argv[1], "partial-ap-stop-wait-budget")) test_partial_ap_stop_wait_budget();
    else if (!strcmp(argv[1], "partial-ap-stop")) test_partial_ap_stop();
    else if (!strcmp(argv[1], "lifecycle-owner-roles")) test_lifecycle_owner_roles();
    else if (!strcmp(argv[1], "ap-validation")) test_ap_validation();
    else if (!strcmp(argv[1], "shutdown-callback-barrier")) test_shutdown_callback_barrier();
    else if (!strcmp(argv[1], "restart-acquire-barrier")) test_restart_acquire_barrier();
    else if (!strcmp(argv[1], "application-settings-owner")) test_application_settings_owner();
    else if (!strcmp(argv[1], "lifecycle-reservation")) test_lifecycle_reservation();
    else if (!strcmp(argv[1], "lifecycle-unregister")) test_lifecycle_unregister_suffix();
    else if (!strcmp(argv[1], "lifecycle-exhaustion")) test_lifecycle_exhaustion();
    else if (!strncmp(argv[1], "restart-failure-", 16)) test_lifecycle_restart_failure(atoi(argv[1] + 16));
    else if (!strcmp(argv[1], "retire-shared")) test_retire_shared();
    else if (!strcmp(argv[1], "retire-operation")) test_retire_operation();
    else if (!strcmp(argv[1], "retire-stop-error")) test_retire_stop_error();
    else if (!strcmp(argv[1], "retire-acquire-race")) test_retire_acquire_race();
    else if (!strcmp(argv[1], "channel-event")) test_channel_event_conflict();
    else if (!strcmp(argv[1], "channel-event-restart")) test_channel_event_error_and_restart();
    else if (!strcmp(argv[1], "channel-event-write-race")) test_channel_event_overtakes_write();
    else if (!strcmp(argv[1], "channel-event-query-race")) test_channel_event_overtakes_query();
    else if (!strcmp(argv[1], "event-loop-error")) test_channel_registration_failure(true);
    else if (!strcmp(argv[1], "channel-handler-error")) test_channel_registration_failure(false);
    else if (!strcmp(argv[1], "operation-fixed")) test_operation_fixed_conflict();
    else if (!strcmp(argv[1], "operation-identity")) test_operation_identity_and_owner();
    else if (!strcmp(argv[1], "operation-exhaustion")) test_operation_exhaustion();
    else if (!strcmp(argv[1], "operation-mutation")) test_operation_mutation_guard();
    else if (!strcmp(argv[1], "operation-channel-race")) test_operation_channel_race();
    else if (!strcmp(argv[1], "stale")) test_stale();
    else if (!strcmp(argv[1], "channel")) test_channel();
    else if (!strcmp(argv[1], "concurrent")) test_concurrent(false);
    else if (!strcmp(argv[1], "release-race")) test_concurrent(true);
    else if (!strcmp(argv[1], "exhaustion")) test_exhaustion();
    else if (!strcmp(argv[1], "promiscuous-cleanup")) test_promiscuous_cleanup();
    else if (!strcmp(argv[1], "promiscuous-shared-identity")) test_promiscuous_shared_identity();
    else if (!strcmp(argv[1], "promiscuous-failed-acquire")) test_promiscuous_failed_acquire();
    else if (!strcmp(argv[1], "promiscuous-identity-exhausted")) test_promiscuous_identity_exhausted();
    else if (!strcmp(argv[1], "promiscuous-rx-union")) test_promiscuous_rx_union();
    else if (!strcmp(argv[1], "promiscuous-rx-drain")) test_promiscuous_rx_drain();
    else if (!strcmp(argv[1], "promiscuous-rx-failure")) test_promiscuous_rx_failure();
    else if (!strcmp(argv[1], "promiscuous-rx-capacity")) test_promiscuous_rx_capacity();
    else if (!strcmp(argv[1], "capacity")) test_registry_capacity();
    else if (!strcmp(argv[1], "shared-release")) test_shared_channel_release();
    else if (!strcmp(argv[1], "lifecycle")) test_lifecycle();
    else if (!strcmp(argv[1], "stop-error")) test_cleanup_suffix(9);
    else if (!strcmp(argv[1], "deinit-error")) test_cleanup_suffix(10);
    else if (!strcmp(argv[1], "mode-union")) test_mode_union();
    else if (!strcmp(argv[1], "channel-drift")) test_home_channel_and_drift();
    else if (!strncmp(argv[1], "cleanup-", 8)) test_failed_start_cleanup(atoi(argv[1] + 8));
    else if (!strcmp(argv[1], "stop-acquire-race")) test_shutdown_acquire_race(9);
    else if (!strcmp(argv[1], "deinit-acquire-race")) test_shutdown_acquire_race(10);
    else if (!strcmp(argv[1], "global-settings")) test_global_settings();
    else if (!strcmp(argv[1], "global-settings-errors")) test_global_settings_errors();
    else if (!strcmp(argv[1], "power-save-release-race")) test_setting_release_race(11);
    else if (!strcmp(argv[1], "tx-power-release-race")) test_setting_release_race(13);
    else test_failure(atoi(argv[1]));
    return 0;
}
