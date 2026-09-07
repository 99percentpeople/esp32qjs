/* Compile the production Radio, replacing only SDK/RTOS calls. Each CTest
 * case is a new process: boot-scoped once state is never reset for testing. */
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../components/esp32_mquickjs/src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c"

_Thread_local int test_critical_depth;
static int fail_at, calls[10];
static wifi_mode_t driver_mode;
static uint8_t driver_channel = 1;
static bool driver_promiscuous;
static pthread_mutex_t schedule_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t schedule_cond = PTHREAD_COND_INITIALIZER;
static int pause_at, paused, resume_driver, mutex_contended;
static atomic_bool release_entered, release_finished;

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
esp_err_t esp_wifi_set_storage(int s) { (void)s; return driver_call(3); }
esp_err_t esp_wifi_get_mode(wifi_mode_t *m) { *m = driver_mode; return driver_call(4); }
esp_err_t esp_wifi_set_mode(wifi_mode_t m) { int e = driver_call(5); if (!e) driver_mode = m; return e; }
esp_err_t esp_wifi_start(void) { return driver_call(6); }
esp_err_t esp_wifi_get_country(wifi_country_t *c) { c->schan = 1; c->nchan = 11; return ESP_OK; }
esp_err_t esp_wifi_get_channel(uint8_t *p, wifi_second_chan_t *s) { *p = driver_channel; *s = WIFI_SECOND_CHAN_NONE; return ESP_OK; }
esp_err_t esp_wifi_set_channel(uint8_t p, wifi_second_chan_t s) { (void)s; int e = driver_call(7); if (!e) driver_channel = p; return e; }
esp_err_t esp_wifi_get_max_tx_power(int8_t *p) { *p = 80; return ESP_OK; }
esp_err_t esp_wifi_get_ps(wifi_ps_type_t *p) { *p = WIFI_PS_NONE; return ESP_OK; }
esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t *a) { (void)a; return ESP_ERR_WIFI_NOT_CONNECT; }
esp_err_t esp_wifi_get_config(int i, wifi_config_t *c) { (void)i; c->ap.channel = 0; return ESP_OK; }
esp_err_t esp_wifi_get_promiscuous(bool *e) { *e = driver_promiscuous; return ESP_OK; }
esp_err_t esp_wifi_set_promiscuous(bool e) { int r = driver_call(8); if (!r) driver_promiscuous = e; return r; }

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
    int before[10]; memcpy(before, calls, sizeof(calls));
    assert(esp32_mquickjs_wifi_radio_ensure_started(&a) == 0x700 + stage);
    assert(memcmp(before, calls, sizeof(calls)) == 0);
#ifdef RADIO_DIAGNOSTICS_TEST
    esp32_mquickjs_wifi_radio_status_t status;
    assert(esp32_mquickjs_wifi_radio_get_status(&status) == ESP_OK);
    assert(status.restart_required && status.fault_error == 0x700 + stage);
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
    assert(esp32_mquickjs_wifi_radio_set_channel(&b, 6, WIFI_SECOND_CHAN_NONE) == ESP_ERR_INVALID_STATE);
    fail_at = 7;
    assert(esp32_mquickjs_wifi_radio_set_channel(&a, 11, WIFI_SECOND_CHAN_NONE) == 0x707);
    assert(s_radio.fixed_channel_owner.primary_channel == 6 && driver_channel == 6);
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
static void test_promiscuous_cleanup(void)
{
    esp32_mquickjs_wifi_radio_lease_t a = acquire();
    esp32_mquickjs_wifi_radio_promiscuous_lease_t prom;
    assert(esp32_mquickjs_wifi_radio_ensure_started(&a) == ESP_OK);
    assert(esp32_mquickjs_wifi_radio_acquire_promiscuous(&a, &prom) == ESP_OK);
    fail_at = 8;
    esp32_mquickjs_wifi_radio_release_promiscuous(&prom);
    assert(prom.acquired && s_radio.promiscuous_claimed && driver_promiscuous);
    esp32_mquickjs_wifi_radio_release(&a);
    assert(a.acquired && wifi_radio_lease_valid(&a));
    fail_at = 0;
    esp32_mquickjs_wifi_radio_release_promiscuous(&prom);
    assert(!prom.acquired && !s_radio.promiscuous_claimed && !driver_promiscuous);
    esp32_mquickjs_wifi_radio_release(&a);
    assert(!a.acquired);
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

int main(int argc, char **argv)
{
    assert(argc == 2);
    if (!strcmp(argv[1], "stale")) test_stale();
    else if (!strcmp(argv[1], "channel")) test_channel();
    else if (!strcmp(argv[1], "concurrent")) test_concurrent(false);
    else if (!strcmp(argv[1], "release-race")) test_concurrent(true);
    else if (!strcmp(argv[1], "exhaustion")) test_exhaustion();
    else if (!strcmp(argv[1], "promiscuous-cleanup")) test_promiscuous_cleanup();
    else if (!strcmp(argv[1], "capacity")) test_registry_capacity();
    else test_failure(atoi(argv[1]));
    return 0;
}
