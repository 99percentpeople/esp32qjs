#include "esp32_mquickjs_wifi.h"
#include "esp32_mquickjs_memory.h"
#include "esp32_mquickjs_memory_rtos.h"
#include "esp32_mquickjs_wifi_neighbor.h"

#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
#include "esp32_mquickjs_core.h"
#include "esp32_mquickjs_event_queue.h"
#include "esp32_mquickjs_options.h"
#include "esp_heap_caps.h"
#include "esp_wifi_he_types.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/* All stored payloads are explicit value copies. SDK pointers/padding/secrets
 * never enter ingress or subscriber queues. */
typedef enum {
    WATCH_EMPTY, WATCH_UNREVIEWED, WATCH_SENSITIVE, WATCH_SCAN,
    WATCH_CONNECTED, WATCH_DISCONNECTED, WATCH_AUTH, WATCH_AP_CONNECTED,
    WATCH_AP_DISCONNECTED, WATCH_PROBE, WATCH_RSSI, WATCH_CHANNEL,
    WATCH_FTM, WATCH_ACTION, WATCH_ROC, WATCH_WRONG_PASSWORD, WATCH_BEACON_OFFSET,
    WATCH_ITWT_SETUP, WATCH_BTWT_SETUP, WATCH_ITWT_TEARDOWN, WATCH_BTWT_TEARDOWN,
    WATCH_ITWT_PROBE, WATCH_ITWT_SUSPEND, WATCH_TWT_WAKEUP, WATCH_NEIGHBOR,
} wifi_watch_kind_t;
typedef struct { int32_t id; const char *name, *category; wifi_watch_kind_t kind; } wifi_watch_descriptor_t;
static const wifi_watch_descriptor_t s_descriptors[] = {
    {WIFI_EVENT_WIFI_READY, "WIFI_EVENT_WIFI_READY", "radio", WATCH_EMPTY},
    {WIFI_EVENT_SCAN_DONE, "WIFI_EVENT_SCAN_DONE", "scan", WATCH_SCAN},
    {WIFI_EVENT_STA_START, "WIFI_EVENT_STA_START", "station", WATCH_EMPTY},
    {WIFI_EVENT_STA_STOP, "WIFI_EVENT_STA_STOP", "station", WATCH_EMPTY},
    {WIFI_EVENT_STA_CONNECTED, "WIFI_EVENT_STA_CONNECTED", "station", WATCH_CONNECTED},
    {WIFI_EVENT_STA_DISCONNECTED, "WIFI_EVENT_STA_DISCONNECTED", "station", WATCH_DISCONNECTED},
    {WIFI_EVENT_STA_AUTHMODE_CHANGE, "WIFI_EVENT_STA_AUTHMODE_CHANGE", "station", WATCH_AUTH},
    {WIFI_EVENT_STA_WPS_ER_SUCCESS, "WIFI_EVENT_STA_WPS_ER_SUCCESS", "wps", WATCH_SENSITIVE},
    {WIFI_EVENT_STA_WPS_ER_FAILED, "WIFI_EVENT_STA_WPS_ER_FAILED", "wps", WATCH_SENSITIVE},
    {WIFI_EVENT_STA_WPS_ER_TIMEOUT, "WIFI_EVENT_STA_WPS_ER_TIMEOUT", "wps", WATCH_SENSITIVE},
    {WIFI_EVENT_STA_WPS_ER_PIN, "WIFI_EVENT_STA_WPS_ER_PIN", "wps", WATCH_SENSITIVE},
    {WIFI_EVENT_STA_WPS_ER_PBC_OVERLAP, "WIFI_EVENT_STA_WPS_ER_PBC_OVERLAP", "wps", WATCH_SENSITIVE},
    {WIFI_EVENT_AP_START, "WIFI_EVENT_AP_START", "access-point", WATCH_EMPTY},
    {WIFI_EVENT_AP_STOP, "WIFI_EVENT_AP_STOP", "access-point", WATCH_EMPTY},
    {WIFI_EVENT_AP_STACONNECTED, "WIFI_EVENT_AP_STACONNECTED", "access-point", WATCH_AP_CONNECTED},
    {WIFI_EVENT_AP_STADISCONNECTED, "WIFI_EVENT_AP_STADISCONNECTED", "access-point", WATCH_AP_DISCONNECTED},
    {WIFI_EVENT_AP_PROBEREQRECVED, "WIFI_EVENT_AP_PROBEREQRECVED", "access-point", WATCH_PROBE},
    {WIFI_EVENT_FTM_REPORT, "WIFI_EVENT_FTM_REPORT", "ftm", WATCH_FTM},
    {WIFI_EVENT_STA_BSS_RSSI_LOW, "WIFI_EVENT_STA_BSS_RSSI_LOW", "station", WATCH_RSSI},
    {WIFI_EVENT_ACTION_TX_STATUS, "WIFI_EVENT_ACTION_TX_STATUS", "radio", WATCH_ACTION},
    {WIFI_EVENT_ROC_DONE, "WIFI_EVENT_ROC_DONE", "radio", WATCH_ROC},
    {WIFI_EVENT_STA_BEACON_TIMEOUT, "WIFI_EVENT_STA_BEACON_TIMEOUT", "station", WATCH_EMPTY},
    {WIFI_EVENT_CONNECTIONLESS_MODULE_WAKE_INTERVAL_START, "WIFI_EVENT_CONNECTIONLESS_MODULE_WAKE_INTERVAL_START", "radio", WATCH_EMPTY},
    {WIFI_EVENT_AP_WPS_RG_SUCCESS, "WIFI_EVENT_AP_WPS_RG_SUCCESS", "wps", WATCH_SENSITIVE},
    {WIFI_EVENT_AP_WPS_RG_FAILED, "WIFI_EVENT_AP_WPS_RG_FAILED", "wps", WATCH_SENSITIVE},
    {WIFI_EVENT_AP_WPS_RG_TIMEOUT, "WIFI_EVENT_AP_WPS_RG_TIMEOUT", "wps", WATCH_SENSITIVE},
    {WIFI_EVENT_AP_WPS_RG_PIN, "WIFI_EVENT_AP_WPS_RG_PIN", "wps", WATCH_SENSITIVE},
    {WIFI_EVENT_AP_WPS_RG_PBC_OVERLAP, "WIFI_EVENT_AP_WPS_RG_PBC_OVERLAP", "wps", WATCH_SENSITIVE},
    {WIFI_EVENT_ITWT_SETUP, "WIFI_EVENT_ITWT_SETUP", "twt", WATCH_ITWT_SETUP},
    {WIFI_EVENT_ITWT_TEARDOWN, "WIFI_EVENT_ITWT_TEARDOWN", "twt", WATCH_ITWT_TEARDOWN},
    {WIFI_EVENT_ITWT_PROBE, "WIFI_EVENT_ITWT_PROBE", "twt", WATCH_ITWT_PROBE},
    {WIFI_EVENT_ITWT_SUSPEND, "WIFI_EVENT_ITWT_SUSPEND", "twt", WATCH_ITWT_SUSPEND},
    {WIFI_EVENT_TWT_WAKEUP, "WIFI_EVENT_TWT_WAKEUP", "twt", WATCH_TWT_WAKEUP},
    {WIFI_EVENT_BTWT_SETUP, "WIFI_EVENT_BTWT_SETUP", "twt", WATCH_BTWT_SETUP},
    {WIFI_EVENT_BTWT_TEARDOWN, "WIFI_EVENT_BTWT_TEARDOWN", "twt", WATCH_BTWT_TEARDOWN},
    {WIFI_EVENT_NAN_SYNC_STARTED, "WIFI_EVENT_NAN_SYNC_STARTED", "nan", WATCH_SENSITIVE},
    {WIFI_EVENT_NAN_SYNC_STOPPED, "WIFI_EVENT_NAN_SYNC_STOPPED", "nan", WATCH_SENSITIVE},
    {WIFI_EVENT_NAN_SVC_MATCH, "WIFI_EVENT_NAN_SVC_MATCH", "nan", WATCH_SENSITIVE},
    {WIFI_EVENT_NAN_REPLIED, "WIFI_EVENT_NAN_REPLIED", "nan", WATCH_SENSITIVE},
    {WIFI_EVENT_NAN_RECEIVE, "WIFI_EVENT_NAN_RECEIVE", "nan", WATCH_SENSITIVE},
    {WIFI_EVENT_NDP_INDICATION, "WIFI_EVENT_NDP_INDICATION", "nan", WATCH_SENSITIVE},
    {WIFI_EVENT_NDP_CONFIRM, "WIFI_EVENT_NDP_CONFIRM", "nan", WATCH_SENSITIVE},
    {WIFI_EVENT_NDP_TERMINATED, "WIFI_EVENT_NDP_TERMINATED", "nan", WATCH_SENSITIVE},
    {WIFI_EVENT_HOME_CHANNEL_CHANGE, "WIFI_EVENT_HOME_CHANNEL_CHANGE", "radio", WATCH_CHANNEL},
    {WIFI_EVENT_STA_NEIGHBOR_REP, "WIFI_EVENT_STA_NEIGHBOR_REP", "station", WATCH_NEIGHBOR},
    {WIFI_EVENT_AP_WRONG_PASSWORD, "WIFI_EVENT_AP_WRONG_PASSWORD", "access-point", WATCH_WRONG_PASSWORD},
    {WIFI_EVENT_STA_BEACON_OFFSET_UNSTABLE, "WIFI_EVENT_STA_BEACON_OFFSET_UNSTABLE", "station", WATCH_BEACON_OFFSET},
    {WIFI_EVENT_DPP_URI_READY, "WIFI_EVENT_DPP_URI_READY", "dpp", WATCH_SENSITIVE},
    {WIFI_EVENT_DPP_CFG_RECVD, "WIFI_EVENT_DPP_CFG_RECVD", "dpp", WATCH_SENSITIVE},
    {WIFI_EVENT_DPP_FAILED, "WIFI_EVENT_DPP_FAILED", "dpp", WATCH_SENSITIVE},
    {WIFI_EVENT_NAN_PAIRING_INDICATION, "WIFI_EVENT_NAN_PAIRING_INDICATION", "nan", WATCH_SENSITIVE},
    {WIFI_EVENT_NAN_PAIRING_CONFIRM, "WIFI_EVENT_NAN_PAIRING_CONFIRM", "nan", WATCH_SENSITIVE},
    {WIFI_EVENT_NAN_CLUSTER_JOIN, "WIFI_EVENT_NAN_CLUSTER_JOIN", "nan", WATCH_SENSITIVE},
};
_Static_assert(WIFI_EVENT_MAX <= 64, "extend event filter storage before adding IDs");
_Static_assert(sizeof(s_descriptors) / sizeof(s_descriptors[0]) == WIFI_EVENT_MAX,
               "review new SDK Wi-Fi events before publishing them");

/* Reviewed IEEE Neighbor Report fields only; no vendor bytes or SDK pointers. */
typedef esp32_mquickjs_wifi_neighbor_t wifi_watch_neighbor_t;
typedef struct {
    uint32_t refs;
    uint16_t report_length, count, skipped_elements;
    uint8_t dialog_token;
    wifi_watch_neighbor_t neighbors[ESP32_MQUICKJS_WIFI_WATCH_MAX_NEIGHBORS];
} wifi_watch_neighbor_slot_t;
typedef enum {
    WATCH_DATA_OK, WATCH_DATA_INVALID, WATCH_DATA_CAPACITY, WATCH_DATA_TOO_LARGE,
} wifi_watch_data_result_t;

typedef struct {
    uint64_t sequence, timestamp_us;
    int32_t id;
    uint32_t control_generation;
    bool has_data, raw_requested;
    uint8_t data_result;
    union {
        wifi_watch_neighbor_slot_t *neighbor;
        struct {
            uint8_t address[6], ssid[32];
            union { uint8_t ssid_len, count; };
            union {
                struct { int32_t a, b, c, d; };
                uint32_t unsigned_values[4];
                float value;
            };
        };
        struct {
            uint64_t target_wake_time;
            int32_t status, setup_command;
            uint16_t mantissa, twt_id, timeout_ms;
            uint8_t reason, id, exponent, duration, unit, flow_type;
            bool trigger;
        } twt_setup;
        struct {
            int32_t status;
            uint8_t bitmap;
            uint32_t duration_ms[8];
        } twt_suspend;
    };
} wifi_watch_event_t;
typedef struct {
    esp32_mquickjs_runtime_t *runtime;
    esp32_mquickjs_event_queue_t *queue;
    uint64_t after_sequence, mask;
    bool all, raw;
} wifi_watch_source_t;

/* One boot-owned mutex and default-loop observer; neither holds a runtime or
 * driver reference. Payload storage is freed when the last subscription closes. */
static StaticSemaphore_t s_watch_mutex_storage;
static _Atomic(SemaphoreHandle_t) s_watch_mutex;
static esp_event_handler_instance_t s_watch_instance;
static QueueHandle_t s_ingress;
static wifi_watch_source_t *s_sources[ESP32_MQUICKJS_WIFI_MAX_WATCHERS];
static uint64_t s_sequence;
static _Atomic uint32_t s_ingress_dropped;
static uint32_t s_ingress_high_water;
#define WIFI_WATCH_MAX_SEQUENCE UINT64_C(9007199254740991)

/* Separate lock: EventQueue drops may run with its send lock held. Never take
 * the watch mutex from a drop, allocate/free under a port critical section, or
 * allow a new pool while the previous pool is still being freed. */
static portMUX_TYPE s_neighbor_mux = portMUX_INITIALIZER_UNLOCKED;
static wifi_watch_neighbor_slot_t *s_neighbor_pool;
static enum { NEIGHBOR_NONE, NEIGHBOR_OPEN, NEIGHBOR_RETIRED, NEIGHBOR_REAPING } s_neighbor_state;
static uint32_t s_neighbor_active;

/* Caller holds the watch mutex, which serializes open and retirement. */
static esp_err_t wifi_watch_neighbor_open(void)
{
    portENTER_CRITICAL(&s_neighbor_mux);
    bool ready = s_neighbor_state == NEIGHBOR_OPEN;
    bool empty = s_neighbor_state == NEIGHBOR_NONE;
    portEXIT_CRITICAL(&s_neighbor_mux);
    if (ready) return ESP_OK;
    if (!empty) return ESP_ERR_INVALID_STATE;
    wifi_watch_neighbor_slot_t *pool = esp32_mquickjs_memory_wireless_calloc("wifi", ESP32_MQUICKJS_WIFI_WATCH_NEIGHBOR_SLOTS, sizeof(*pool), ESP32_MQUICKJS_MEMORY_DEFAULT, ESP32_MQUICKJS_MEMORY_BUDGET_POOL);
    if (pool == NULL) return ESP_ERR_NO_MEM;
    portENTER_CRITICAL(&s_neighbor_mux);
    s_neighbor_pool = pool;
    s_neighbor_state = NEIGHBOR_OPEN;
    portEXIT_CRITICAL(&s_neighbor_mux);
    return ESP_OK;
}

static void wifi_watch_neighbor_free(wifi_watch_neighbor_slot_t *pool)
{
    if (pool == NULL) return;
    esp32_mquickjs_memory_payload_free(pool);
    portENTER_CRITICAL(&s_neighbor_mux);
    s_neighbor_pool = NULL;
    s_neighbor_state = NEIGHBOR_NONE;
    portEXIT_CRITICAL(&s_neighbor_mux);
}

static void wifi_watch_neighbor_retire(void)
{
    wifi_watch_neighbor_slot_t *reap = NULL;
    portENTER_CRITICAL(&s_neighbor_mux);
    if (s_neighbor_state == NEIGHBOR_OPEN) {
        s_neighbor_state = NEIGHBOR_RETIRED;
        if (s_neighbor_active == 0) {
            s_neighbor_state = NEIGHBOR_REAPING;
            reap = s_neighbor_pool;
        }
    }
    portEXIT_CRITICAL(&s_neighbor_mux);
    wifi_watch_neighbor_free(reap);
}

static wifi_watch_neighbor_slot_t *wifi_watch_neighbor_acquire(void)
{
    wifi_watch_neighbor_slot_t *slot = NULL;
    portENTER_CRITICAL(&s_neighbor_mux);
    if (s_neighbor_state == NEIGHBOR_OPEN) {
        for (size_t i = 0; i < ESP32_MQUICKJS_WIFI_WATCH_NEIGHBOR_SLOTS; ++i) {
            if (s_neighbor_pool[i].refs == 0) {
                slot = &s_neighbor_pool[i];
                slot->refs = 1;
                ++s_neighbor_active;
                break;
            }
        }
    }
    portEXIT_CRITICAL(&s_neighbor_mux);
    return slot;
}

static void wifi_watch_neighbor_retain(wifi_watch_neighbor_slot_t *slot)
{
    if (slot == NULL) return;
    portENTER_CRITICAL(&s_neighbor_mux);
    ++slot->refs; /* Bounded by ingress + four queues/receivers + fanout. */
    portEXIT_CRITICAL(&s_neighbor_mux);
}

static void wifi_watch_neighbor_release(wifi_watch_neighbor_slot_t *slot)
{
    if (slot == NULL) return;
    wifi_watch_neighbor_slot_t *reap = NULL;
    portENTER_CRITICAL(&s_neighbor_mux);
    if (--slot->refs == 0) {
        --s_neighbor_active;
        if (s_neighbor_active == 0 && s_neighbor_state == NEIGHBOR_RETIRED) {
            s_neighbor_state = NEIGHBOR_REAPING;
            reap = s_neighbor_pool;
        }
    }
    portEXIT_CRITICAL(&s_neighbor_mux);
    wifi_watch_neighbor_free(reap);
}

static void wifi_watch_event_drop(void *opaque_event, void *opaque)
{
    (void)opaque;
    wifi_watch_event_t *event = opaque_event;
    if (event->id != WIFI_EVENT_STA_NEIGHBOR_REP) return;
    wifi_watch_neighbor_slot_t *slot = event->neighbor;
    event->neighbor = NULL;
    wifi_watch_neighbor_release(slot);
}

/* SDK esp_common.c copies report_len bytes, including the dialog token, into
 * the event-loop allocation. Validate every outer/inner TLV before reading it.
 * WLAN_EID_NEIGHBOR_REPORT=52, WNM_NEIGHBOR_BSS_TRANSITION_CANDIDATE=3 in the
 * pinned supplicant's ieee802_11_defs.h. Unknown TLVs are counted, not copied. */
static wifi_watch_data_result_t wifi_watch_neighbor_parse(
    wifi_watch_neighbor_slot_t *slot, const uint8_t *bytes, size_t length)
{
    if (length > ESP32_MQUICKJS_WIFI_WATCH_MAX_REPORT_BYTES) return WATCH_DATA_TOO_LARGE;
    if (bytes == NULL || length == 0) return WATCH_DATA_INVALID;
    slot->report_length = length;
    slot->dialog_token = bytes[0];
    slot->count = slot->skipped_elements = 0;
    size_t offset = 1;
    while (offset < length) {
        if (length - offset < 2) return WATCH_DATA_INVALID;
        uint8_t id = bytes[offset], size = bytes[offset + 1];
        offset += 2;
        if (size > length - offset) return WATCH_DATA_INVALID;
        const uint8_t *body = bytes + offset;
        offset += size;
        if (id != 52) { ++slot->skipped_elements; continue; }
        if (size < 13) return WATCH_DATA_INVALID;
        if (slot->count == ESP32_MQUICKJS_WIFI_WATCH_MAX_NEIGHBORS) return WATCH_DATA_TOO_LARGE;
        wifi_watch_neighbor_t *neighbor = &slot->neighbors[slot->count++];
        if (!esp32_mquickjs_wifi_neighbor_decode(neighbor, body, size)) return WATCH_DATA_INVALID;
    }
    return WATCH_DATA_OK;
}

static void wifi_watch_drop(void)
{
    uint32_t old = atomic_load(&s_ingress_dropped);
    while (old != UINT32_MAX && !atomic_compare_exchange_weak(&s_ingress_dropped, &old, old + 1U)) {}
}

static const wifi_watch_descriptor_t *wifi_watch_descriptor(int32_t id)
{
    for (size_t i = 0; i < sizeof(s_descriptors) / sizeof(s_descriptors[0]); ++i)
        if (s_descriptors[i].id == id) return &s_descriptors[i];
    return NULL;
}

void esp32_mquickjs_wifi_watch_capture(int32_t id, const void *data, uint32_t generation)
{
    if (s_watch_mutex == NULL) return;
    if (xSemaphoreTake(s_watch_mutex, 0) != pdTRUE) { wifi_watch_drop(); return; }
    if (s_ingress == NULL) goto done;
    if (s_sequence == WIFI_WATCH_MAX_SEQUENCE) { wifi_watch_drop(); goto done; }
    wifi_watch_event_t event = {.id = id, .sequence = ++s_sequence,
        .timestamp_us = esp_timer_get_time(), .control_generation = generation};
    const wifi_watch_descriptor_t *descriptor = wifi_watch_descriptor(id);
    wifi_watch_kind_t kind = descriptor == NULL ? WATCH_UNREVIEWED : descriptor->kind;
    event.has_data = kind == WATCH_EMPTY || (data != NULL && kind != WATCH_UNREVIEWED && kind != WATCH_SENSITIVE);
    if (kind == WATCH_NEIGHBOR) {
        bool interested = false;
        for (size_t i = 0; i < ESP32_MQUICKJS_WIFI_MAX_WATCHERS; ++i)
            if (s_sources[i] != NULL && (s_sources[i]->all ||
                (s_sources[i]->mask & (UINT64_C(1) << WIFI_EVENT_STA_NEIGHBOR_REP)))) interested = true;
        if (!interested) goto done;
        event.has_data = true; /* NULL is the SDK's no-report notification. */
        if (data != NULL) {
            const wifi_event_neighbor_report_t *report = data;
            if (report->report_len > ESP32_MQUICKJS_WIFI_WATCH_MAX_REPORT_BYTES)
                event.data_result = WATCH_DATA_TOO_LARGE;
            else if (report->report_len == 0) event.data_result = WATCH_DATA_INVALID;
            else if ((event.neighbor = wifi_watch_neighbor_acquire()) == NULL)
                event.data_result = WATCH_DATA_CAPACITY;
            else event.data_result = wifi_watch_neighbor_parse(event.neighbor, report->n_report, report->report_len);
            if (event.data_result != WATCH_DATA_OK) {
                event.has_data = false;
                wifi_watch_event_drop(&event, NULL);
            }
        }
    }
    if (data != NULL) switch (kind) {
    case WATCH_SCAN: {
        const wifi_event_sta_scan_done_t *v = data;
        event.a = v->status; event.b = v->number; event.c = v->scan_id; break;
    }
    case WATCH_CONNECTED: {
        const wifi_event_sta_connected_t *v = data;
        if (v->ssid_len > sizeof(event.ssid)) { event.has_data = false; break; }
        event.ssid_len = v->ssid_len; memcpy(event.ssid, v->ssid, v->ssid_len);
        memcpy(event.address, v->bssid, 6);
        event.a = v->channel; event.b = v->authmode; event.c = v->aid; break;
    }
    case WATCH_DISCONNECTED: {
        const wifi_event_sta_disconnected_t *v = data;
        if (v->ssid_len > sizeof(event.ssid)) { event.has_data = false; break; }
        event.ssid_len = v->ssid_len; memcpy(event.ssid, v->ssid, v->ssid_len);
        memcpy(event.address, v->bssid, 6); event.a = v->reason; event.b = v->rssi; break;
    }
    case WATCH_AUTH: {
        const wifi_event_sta_authmode_change_t *v = data;
        event.a = v->old_mode; event.b = v->new_mode; break;
    }
    case WATCH_AP_CONNECTED: {
        const wifi_event_ap_staconnected_t *v = data;
        memcpy(event.address, v->mac, 6); event.a = v->aid; event.b = v->is_mesh_child; break;
    }
    case WATCH_AP_DISCONNECTED: {
        const wifi_event_ap_stadisconnected_t *v = data;
        memcpy(event.address, v->mac, 6); event.a = v->aid; event.b = v->is_mesh_child;
        event.c = v->reason; break;
    }
    case WATCH_PROBE: {
        const wifi_event_ap_probe_req_rx_t *v = data;
        memcpy(event.address, v->mac, 6); event.a = v->rssi; break;
    }
    case WATCH_RSSI: event.a = ((const wifi_event_bss_rssi_low_t *)data)->rssi; break;
    case WATCH_CHANNEL: {
        const wifi_event_home_channel_change_t *v = data;
        event.a = v->old_chan; event.b = v->old_snd; event.c = v->new_chan; event.d = v->new_snd; break;
    }
    case WATCH_FTM: {
        const wifi_event_ftm_report_t *v = data;
        memcpy(event.address, v->peer_mac, 6); event.a = v->status;
        /* Failed measurements do not promise initialized/meaningful values.
         * This observer never consumes the SDK-owned detailed FTM report. */
        if (v->status == FTM_STATUS_SUCCESS) {
            event.unsigned_values[1] = v->rtt_raw;
            event.unsigned_values[2] = v->rtt_est;
            event.unsigned_values[3] = v->dist_est;
            event.count = v->ftm_report_num_entries;
        }
        break;
    }
    case WATCH_ACTION: {
        const wifi_event_action_tx_status_t *v = data;
        event.a = v->ifx; event.b = v->status; event.c = v->op_id; event.d = v->channel;
        break; /* context is an opaque native cookie; never copy/export it. */
    }
    case WATCH_ROC: {
        const wifi_event_roc_done_t *v = data;
        event.a = v->status; event.b = v->op_id; event.c = v->channel;
        break; /* No native context/pointer is copied into watch storage. */
    }
    case WATCH_WRONG_PASSWORD:
        memcpy(event.address, ((const wifi_event_ap_wrong_password_t *)data)->mac, 6);
        break;
    case WATCH_BEACON_OFFSET:
        event.value = ((const wifi_event_sta_beacon_offset_unstable_t *)data)->beacon_success_rate;
        if (!isfinite(event.value)) event.has_data = false;
        break;
    case WATCH_ITWT_SETUP: {
        const wifi_event_sta_itwt_setup_t *v = data;
        event.twt_setup.status = v->status;
        if (v->status == 1) { /* The pinned SDK documents 1, not ESP_OK. */
            event.twt_setup.target_wake_time = v->target_wake_time;
            event.twt_setup.setup_command = v->config.setup_cmd;
            event.twt_setup.trigger = v->config.trigger;
            event.twt_setup.flow_type = v->config.flow_type;
            event.twt_setup.id = v->config.flow_id;
            event.twt_setup.exponent = v->config.wake_invl_expn;
            event.twt_setup.unit = v->config.wake_duration_unit;
            event.twt_setup.duration = v->config.min_wake_dura;
            event.twt_setup.mantissa = v->config.wake_invl_mant;
            event.twt_setup.twt_id = v->config.twt_id;
            event.twt_setup.timeout_ms = v->config.timeout_time_ms;
        } else event.twt_setup.reason = v->reason;
        break;
    }
    case WATCH_BTWT_SETUP: {
        const wifi_event_sta_btwt_setup_t *v = data;
        event.twt_setup.status = v->status;
        if (v->status == BTWT_SETUP_SUCCESS) {
            event.twt_setup.target_wake_time = v->target_wake_time;
            event.twt_setup.setup_command = v->setup_cmd;
            event.twt_setup.trigger = v->trigger;
            event.twt_setup.flow_type = v->flow_type;
            event.twt_setup.id = v->btwt_id;
            event.twt_setup.exponent = v->wake_invl_expn;
            event.twt_setup.duration = v->min_wake_dura;
            event.twt_setup.mantissa = v->wake_invl_mant;
        } else if (v->status == BTWT_SETUP_TXFAIL) event.twt_setup.reason = v->reason;
        break;
    }
    case WATCH_ITWT_TEARDOWN: {
        const wifi_event_sta_itwt_teardown_t *v = data;
        event.a = v->status; event.b = v->flow_id; break;
    }
    case WATCH_BTWT_TEARDOWN: {
        const wifi_event_sta_btwt_teardown_t *v = data;
        event.a = v->status; event.b = v->btwt_id; break;
    }
    case WATCH_ITWT_PROBE: {
        const wifi_event_sta_itwt_probe_t *v = data;
        event.a = v->status;
        if (v->status != ITWT_PROBE_SUCCESS) event.b = v->reason;
        break;
    }
    case WATCH_ITWT_SUSPEND: {
        const wifi_event_sta_itwt_suspend_t *v = data;
        event.twt_suspend.status = v->status; event.twt_suspend.bitmap = v->flow_id_bitmap;
        for (size_t i = 0; i < 8; ++i)
            if ((v->flow_id_bitmap & (1U << i)) != 0) event.twt_suspend.duration_ms[i] = v->actual_suspend_time_ms[i];
        break;
    }
    case WATCH_TWT_WAKEUP: {
        const wifi_event_sta_twt_wakeup_t *v = data;
        event.a = v->twt_type; event.b = v->flow_id; break;
    }
    default: break; /* Never dereference unreviewed or sensitive payloads. */
    }
    if (xQueueSend(s_ingress, &event, 0) != pdTRUE) { wifi_watch_event_drop(&event, NULL); wifi_watch_drop(); goto done; }
    uint32_t queued = (uint32_t)uxQueueMessagesWaiting(s_ingress);
    if (queued > s_ingress_high_water) s_ingress_high_water = queued;
    for (size_t i = 0; i < ESP32_MQUICKJS_WIFI_MAX_WATCHERS; ++i)
        if (s_sources[i] != NULL) esp32_mquickjs_notify_activity(s_sources[i]->runtime);
done:
    xSemaphoreGive(s_watch_mutex);
}

static void wifi_watch_callback(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)base;
    if (!esp32_mquickjs_wifi_watch_controlled(id))
        esp32_mquickjs_wifi_watch_capture(id, data, 0);
}

static bool wifi_watch_poll(JSContext *ctx, esp32_mquickjs_runtime_t *runtime, void *opaque)
{
    (void)ctx; (void)opaque;
    bool handled = false;
    if (s_watch_mutex == NULL) return false;
    xSemaphoreTake(s_watch_mutex, portMAX_DELAY);
    for (size_t n = 0; s_ingress != NULL && n < ESP32_MQUICKJS_WIFI_WATCH_INGRESS_CAPACITY; ++n) {
        wifi_watch_event_t event;
        if (xQueuePeek(s_ingress, &event, 0) != pdTRUE) break;
        /* This poller runs on the runtime task after Future polling. A control
         * Future must leave its native registration before its observation can
         * become visible; no recursive Future polling and no queue dependency. */
        if (!esp32_mquickjs_wifi_watch_control_ready(event.id, event.control_generation)) break;
        (void)xQueueReceive(s_ingress, &event, 0);
        handled = true;
        for (size_t i = 0; i < ESP32_MQUICKJS_WIFI_MAX_WATCHERS; ++i) {
            wifi_watch_source_t *source = s_sources[i];
            if (source == NULL || source->runtime != runtime || event.sequence <= source->after_sequence) continue;
            if (!source->all && (event.id < 0 || event.id >= 64 || (source->mask & (UINT64_C(1) << event.id)) == 0)) continue;
            event.raw_requested = source->raw;
            wifi_watch_event_t copy = event;
            if (event.id == WIFI_EVENT_STA_NEIGHBOR_REP) wifi_watch_neighbor_retain(event.neighbor);
            if (!esp32_mquickjs_event_queue_send(source->queue, &copy)) wifi_watch_event_drop(&copy, NULL);
        }
        wifi_watch_event_drop(&event, NULL);
    }
    xSemaphoreGive(s_watch_mutex);
    return handled;
}

static bool wifi_watch_number(JSContext *ctx, JSValue *object, const char *key, int32_t number)
{
    return esp32_mquickjs_set_property_ref(ctx, object, key, JS_NewInt32(ctx, number));
}

static bool wifi_watch_twt_setup_properties(JSContext *ctx, JSValue *data,
    const wifi_watch_event_t *event, bool individual)
{
    bool success = event->twt_setup.status == (individual ? 1 : BTWT_SETUP_SUCCESS);
    bool reason_valid = individual ? !success : event->twt_setup.status == BTWT_SETUP_TXFAIL;
    JSGCRef config_ref, time_ref;
    JSValue *config = JS_PushGCRef(ctx, &config_ref);
    JSValue *time = JS_PushGCRef(ctx, &time_ref);
    *config = *time = JS_NULL;
    bool ok = false;
    if (success) {
        *config = JS_NewObject(ctx);
        *time = JS_NewObject(ctx);
        if (JS_IsException(*config) || JS_IsException(*time) ||
            !wifi_watch_number(ctx, config, "setupCommandId", event->twt_setup.setup_command) ||
            !esp32_mquickjs_set_property_ref(ctx, config, "trigger", JS_NewBool(event->twt_setup.trigger)) ||
            !wifi_watch_number(ctx, config, "flowTypeId", event->twt_setup.flow_type) ||
            !wifi_watch_number(ctx, config, individual ? "flowId" : "broadcastId", event->twt_setup.id) ||
            !wifi_watch_number(ctx, config, "wakeIntervalExponent", event->twt_setup.exponent) ||
            !wifi_watch_number(ctx, config, "wakeIntervalMantissa", event->twt_setup.mantissa) ||
            !wifi_watch_number(ctx, config, "minimumWakeDuration", event->twt_setup.duration)) goto done;
        if (individual && (!wifi_watch_number(ctx, config, "wakeDurationUnitId", event->twt_setup.unit) ||
            !wifi_watch_number(ctx, config, "twtId", event->twt_setup.twt_id) ||
            !wifi_watch_number(ctx, config, "timeoutMs", event->twt_setup.timeout_ms))) goto done;
        if (!esp32_mquickjs_set_property_ref(ctx, time, "low", JS_NewUint32(ctx, (uint32_t)event->twt_setup.target_wake_time)) ||
            !esp32_mquickjs_set_property_ref(ctx, time, "high", JS_NewUint32(ctx, (uint32_t)(event->twt_setup.target_wake_time >> 32)))) goto done;
    }
    ok = wifi_watch_number(ctx, data, "statusId", event->twt_setup.status) &&
        esp32_mquickjs_set_property_ref(ctx, data, "reason", reason_valid ? JS_NewUint32(ctx, event->twt_setup.reason) : JS_NULL) &&
        esp32_mquickjs_set_property_ref(ctx, data, "configuration", *config) &&
        esp32_mquickjs_set_property_ref(ctx, data, "targetWakeTime", *time);
done:
    JS_PopGCRef(ctx, &time_ref);
    JS_PopGCRef(ctx, &config_ref);
    return ok;
}

static bool wifi_watch_twt_suspend_properties(JSContext *ctx, JSValue *data,
    const wifi_watch_event_t *event)
{
    JSGCRef times_ref;
    JSValue *times = JS_PushGCRef(ctx, &times_ref);
    *times = JS_NewArray(ctx, 0);
    bool ok = false;
    if (JS_IsException(*times)) goto done;
    for (uint32_t i = 0; i < 8; ++i) {
        JSValue value = (event->twt_suspend.bitmap & (1U << i)) != 0
            ? JS_NewUint32(ctx, event->twt_suspend.duration_ms[i]) : JS_NULL;
        if (JS_IsException(value) || JS_IsException(JS_SetPropertyUint32(ctx, *times, i, value))) goto done;
    }
    ok = wifi_watch_number(ctx, data, "statusId", event->twt_suspend.status) &&
        wifi_watch_number(ctx, data, "flowIdBitmap", event->twt_suspend.bitmap) &&
        esp32_mquickjs_set_property_ref(ctx, data, "actualSuspendTimeMs", *times);
done:
    JS_PopGCRef(ctx, &times_ref);
    return ok;
}

static bool wifi_watch_neighbor_properties(JSContext *ctx, JSValue *data,
    const wifi_watch_neighbor_slot_t *slot)
{
    JSGCRef list_ref, entry_ref;
    JSValue *list = JS_PushGCRef(ctx, &list_ref);
    JSValue *entry = JS_PushGCRef(ctx, &entry_ref);
    *list = JS_NewArray(ctx, 0);
    bool ok = false;
    if (JS_IsException(*list)) goto done;
    for (uint32_t i = 0; slot != NULL && i < slot->count; ++i) {
        const wifi_watch_neighbor_t *v = &slot->neighbors[i];
        char address[18];
        snprintf(address, sizeof(address), "%02x:%02x:%02x:%02x:%02x:%02x",
            v->bssid[0], v->bssid[1], v->bssid[2], v->bssid[3], v->bssid[4], v->bssid[5]);
        *entry = JS_NewObject(ctx);
        if (JS_IsException(*entry) ||
            !esp32_mquickjs_set_property_ref(ctx, entry, "bssid", JS_NewString(ctx, address)) ||
            !esp32_mquickjs_set_property_ref(ctx, entry, "bssidInformation", JS_NewUint32(ctx, v->bssid_information)) ||
            !wifi_watch_number(ctx, entry, "operatingClass", v->operating_class) ||
            !wifi_watch_number(ctx, entry, "channel", v->channel) ||
            !wifi_watch_number(ctx, entry, "phyTypeId", v->phy_type) ||
            !esp32_mquickjs_set_property_ref(ctx, entry, "candidatePreference", v->has_preference ? JS_NewUint32(ctx, v->preference) : JS_NULL) ||
            !wifi_watch_number(ctx, entry, "skippedSubelements", v->skipped_subelements) ||
            JS_IsException(JS_SetPropertyUint32(ctx, *list, i, *entry))) goto done;
    }
    ok = esp32_mquickjs_set_property_ref(ctx, data, "received", JS_NewBool(slot != NULL)) &&
        wifi_watch_number(ctx, data, "reportLength", slot == NULL ? 0 : slot->report_length) &&
        esp32_mquickjs_set_property_ref(ctx, data, "dialogToken", slot == NULL ? JS_NULL : JS_NewUint32(ctx, slot->dialog_token)) &&
        wifi_watch_number(ctx, data, "skippedElements", slot == NULL ? 0 : slot->skipped_elements) &&
        esp32_mquickjs_set_property_ref(ctx, data, "neighbors", *list);
done:
    JS_PopGCRef(ctx, &entry_ref);
    JS_PopGCRef(ctx, &list_ref);
    return ok;
}

static JSValue wifi_watch_value_to_js(JSContext *ctx, const void *opaque_event, void *opaque)
{
    const wifi_watch_event_t *event = opaque_event;
    (void)opaque;
    const wifi_watch_descriptor_t *descriptor = wifi_watch_descriptor(event->id);
    wifi_watch_kind_t kind = descriptor == NULL ? WATCH_UNREVIEWED : descriptor->kind;
    const char *unavailable = kind == WATCH_SENSITIVE ? "sensitive" :
        kind == WATCH_UNREVIEWED ? "not-reviewed" :
        event->data_result == WATCH_DATA_CAPACITY ? "capacity" :
        event->data_result == WATCH_DATA_TOO_LARGE ? "too-large" : !event->has_data ? "unsupported-layout" : NULL;
    char address[18];
    snprintf(address, sizeof(address), "%02x:%02x:%02x:%02x:%02x:%02x",
        event->address[0], event->address[1], event->address[2], event->address[3], event->address[4], event->address[5]);
    JSGCRef result_ref, data_ref;
    JSValue *result = JS_PushGCRef(ctx, &result_ref);
    JSValue *data = JS_PushGCRef(ctx, &data_ref);
    *result = JS_NewObject(ctx);
    *data = event->has_data ? JS_NewObject(ctx) : JS_NULL;
    if (JS_IsException(*result) || JS_IsException(*data)) goto fail;
    if (event->has_data) {
        if (kind == WATCH_CONNECTED || kind == WATCH_DISCONNECTED) {
            if (!esp32_mquickjs_wifi_set_ssid_properties(ctx, data, event->ssid, event->ssid_len) ||
                !esp32_mquickjs_set_property_ref(ctx, data, "bssid", JS_NewString(ctx, address))) goto fail;
        } else if (kind == WATCH_AP_CONNECTED || kind == WATCH_AP_DISCONNECTED || kind == WATCH_PROBE || kind == WATCH_WRONG_PASSWORD) {
            if (!esp32_mquickjs_set_property_ref(ctx, data, "address", JS_NewString(ctx, address))) goto fail;
        }
        bool ok = true;
        switch (kind) {
        case WATCH_NEIGHBOR:
            ok = wifi_watch_neighbor_properties(ctx, data, event->neighbor); break;
        case WATCH_SCAN:
            ok = esp32_mquickjs_set_property_ref(ctx, data, "status", JS_NewUint32(ctx, (uint32_t)event->a)) &&
                wifi_watch_number(ctx, data, "number", event->b) && wifi_watch_number(ctx, data, "scanId", event->c); break;
        case WATCH_CONNECTED:
            ok = wifi_watch_number(ctx, data, "channel", event->a) && wifi_watch_number(ctx, data, "authModeId", event->b) && wifi_watch_number(ctx, data, "aid", event->c); break;
        case WATCH_DISCONNECTED:
            ok = wifi_watch_number(ctx, data, "reason", event->a) && wifi_watch_number(ctx, data, "rssi", event->b); break;
        case WATCH_AUTH:
            ok = wifi_watch_number(ctx, data, "oldAuthModeId", event->a) && wifi_watch_number(ctx, data, "newAuthModeId", event->b); break;
        case WATCH_AP_CONNECTED: case WATCH_AP_DISCONNECTED:
            ok = wifi_watch_number(ctx, data, "aid", event->a) &&
                esp32_mquickjs_set_property_ref(ctx, data, "isMeshChild", JS_NewBool(event->b));
            if (ok && kind == WATCH_AP_DISCONNECTED) ok = wifi_watch_number(ctx, data, "reason", event->c);
            break;
        case WATCH_PROBE: case WATCH_RSSI: ok = wifi_watch_number(ctx, data, "rssi", event->a); break;
        case WATCH_CHANNEL:
            ok = wifi_watch_number(ctx, data, "oldChannel", event->a) && wifi_watch_number(ctx, data, "oldSecondaryChannelId", event->b) &&
                wifi_watch_number(ctx, data, "newChannel", event->c) && wifi_watch_number(ctx, data, "newSecondaryChannelId", event->d); break;
        case WATCH_FTM: {
            bool success = event->a == FTM_STATUS_SUCCESS;
            ok = esp32_mquickjs_set_property_ref(ctx, data, "peerAddress", JS_NewString(ctx, address)) &&
                wifi_watch_number(ctx, data, "statusId", event->a) &&
                esp32_mquickjs_set_property_ref(ctx, data, "rttRawNs", success ? JS_NewUint32(ctx, event->unsigned_values[1]) : JS_NULL) &&
                esp32_mquickjs_set_property_ref(ctx, data, "rttEstimatedNs", success ? JS_NewUint32(ctx, event->unsigned_values[2]) : JS_NULL) &&
                esp32_mquickjs_set_property_ref(ctx, data, "distanceCm", success ? JS_NewUint32(ctx, event->unsigned_values[3]) : JS_NULL) &&
                esp32_mquickjs_set_property_ref(ctx, data, "reportEntries", success ? JS_NewUint32(ctx, event->count) : JS_NULL);
            break;
        }
        case WATCH_ACTION:
            ok = wifi_watch_number(ctx, data, "interfaceId", event->a) && wifi_watch_number(ctx, data, "statusId", event->b) &&
                wifi_watch_number(ctx, data, "operationId", event->c) && wifi_watch_number(ctx, data, "channel", event->d);
            break;
        case WATCH_ROC:
            ok = wifi_watch_number(ctx, data, "statusId", event->a) && wifi_watch_number(ctx, data, "operationId", event->b) &&
                wifi_watch_number(ctx, data, "channel", event->c);
            break;
        case WATCH_BEACON_OFFSET:
            ok = esp32_mquickjs_set_property_ref(ctx, data, "beaconSuccessRate", JS_NewFloat64(ctx, event->value));
            break;
        case WATCH_ITWT_SETUP: case WATCH_BTWT_SETUP:
            ok = wifi_watch_twt_setup_properties(ctx, data, event, kind == WATCH_ITWT_SETUP); break;
        case WATCH_ITWT_TEARDOWN: case WATCH_BTWT_TEARDOWN:
            ok = wifi_watch_number(ctx, data, "statusId", event->a) &&
                wifi_watch_number(ctx, data, kind == WATCH_ITWT_TEARDOWN ? "flowId" : "broadcastId", event->b); break;
        case WATCH_ITWT_PROBE:
            ok = wifi_watch_number(ctx, data, "statusId", event->a) &&
                esp32_mquickjs_set_property_ref(ctx, data, "reason", event->a != ITWT_PROBE_SUCCESS ? JS_NewUint32(ctx, event->b) : JS_NULL); break;
        case WATCH_ITWT_SUSPEND:
            ok = wifi_watch_twt_suspend_properties(ctx, data, event); break;
        case WATCH_TWT_WAKEUP:
            ok = wifi_watch_number(ctx, data, "typeId", event->a) && wifi_watch_number(ctx, data, "flowId", event->b); break;
        default: break;
        }
        if (!ok) goto fail;
    }
    if (!esp32_mquickjs_set_property_ref(ctx, result, "sequence", JS_NewFloat64(ctx, event->sequence)) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "timestampUs", JS_NewFloat64(ctx, event->timestamp_us)) ||
        !wifi_watch_number(ctx, result, "id", event->id) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "name", JS_NewString(ctx, descriptor == NULL ? "WIFI_EVENT_UNKNOWN" : descriptor->name)) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "category", JS_NewString(ctx, descriptor == NULL ? "unknown" : descriptor->category)) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "data", *data) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "dataUnavailableReason", unavailable == NULL ? JS_NULL : JS_NewString(ctx, unavailable))) goto fail;
    if (event->raw_requested && !esp32_mquickjs_set_property_ref(ctx, result, "rawEventDataUnavailableReason",
        JS_NewString(ctx, kind == WATCH_SENSITIVE ? "sensitive" : "not-reviewed"))) goto fail;
    JS_PopGCRef(ctx, &data_ref);
    return JS_PopGCRef(ctx, &result_ref);
fail:
    JS_PopGCRef(ctx, &data_ref);
    JS_PopGCRef(ctx, &result_ref);
    return JS_EXCEPTION;
}

static JSValue wifi_watch_to_js(JSContext *ctx, const void *opaque_event, void *opaque)
{
    JSValue value = wifi_watch_value_to_js(ctx, opaque_event, opaque);
    /* EventQueue marks finish before conversion, including OOM. It drops only
     * unconverted events. Consume our owned native reference in both outcomes. */
    wifi_watch_event_drop((void *)opaque_event, NULL);
    return value;
}

static void wifi_watch_closed(void *opaque)
{
    wifi_watch_source_t *source = opaque;
    xSemaphoreTake(s_watch_mutex, portMAX_DELAY);
    bool any = false;
    for (size_t i = 0; i < ESP32_MQUICKJS_WIFI_MAX_WATCHERS; ++i) {
        if (s_sources[i] == source) s_sources[i] = NULL;
        any = any || s_sources[i] != NULL;
    }
    if (!any) {
        if (s_ingress != NULL) {
            wifi_watch_event_t event;
            while (xQueueReceive(s_ingress, &event, 0) == pdTRUE) wifi_watch_event_drop(&event, NULL);
            esp32_mquickjs_memory_queue_delete(s_ingress);
            s_ingress = NULL;
        }
        wifi_watch_neighbor_retire();
    }
    xSemaphoreGive(s_watch_mutex);
    esp32_mquickjs_memory_payload_free(source);
}

void esp32_mquickjs_deinit_wifi_watch_runtime(void)
{
    if (s_watch_mutex == NULL) return;
    for (;;) {
        esp32_mquickjs_event_queue_t *queue = NULL;
        xSemaphoreTake(s_watch_mutex, portMAX_DELAY);
        for (size_t i = 0; i < ESP32_MQUICKJS_WIFI_MAX_WATCHERS; ++i)
            if (s_sources[i] != NULL) { queue = s_sources[i]->queue; break; }
        xSemaphoreGive(s_watch_mutex);
        if (queue == NULL) break;
        (void)esp32_mquickjs_event_queue_close(queue);
    }
}

void esp32_mquickjs_wifi_watch_reset_counters(void)
{
    if (s_watch_mutex != NULL) xSemaphoreTake(s_watch_mutex, portMAX_DELAY);
    atomic_store(&s_ingress_dropped, 0);
    s_ingress_high_water = s_ingress == NULL ? 0 : (uint32_t)uxQueueMessagesWaiting(s_ingress);
    if (s_watch_mutex != NULL) xSemaphoreGive(s_watch_mutex);
}

JSValue esp32_mquickjs_wifi_watch_status(JSContext *ctx)
{
    uint32_t count = 0, queued = 0, high_water = 0;
    bool exhausted = false;
    if (s_watch_mutex != NULL) {
        xSemaphoreTake(s_watch_mutex, portMAX_DELAY);
        for (size_t i = 0; i < ESP32_MQUICKJS_WIFI_MAX_WATCHERS; ++i) count += s_sources[i] != NULL;
        if (s_ingress != NULL) queued = uxQueueMessagesWaiting(s_ingress);
        high_water = s_ingress_high_water;
        exhausted = s_sequence == WIFI_WATCH_MAX_SEQUENCE;
        xSemaphoreGive(s_watch_mutex);
    }
    portENTER_CRITICAL(&s_neighbor_mux);
    uint32_t active = s_neighbor_active;
    bool allocated = s_neighbor_state != NEIGHBOR_NONE;
    bool retired = s_neighbor_state == NEIGHBOR_RETIRED || s_neighbor_state == NEIGHBOR_REAPING;
    portEXIT_CRITICAL(&s_neighbor_mux);
    JSGCRef result_ref;
    JSValue *result = JS_PushGCRef(ctx, &result_ref);
    *result = JS_NewObject(ctx);
    if (JS_IsException(*result) || !wifi_watch_number(ctx, result, "subscribers", count) ||
        !wifi_watch_number(ctx, result, "ingressQueued", queued) ||
        !wifi_watch_number(ctx, result, "ingressHighWater", high_water) ||
        !wifi_watch_number(ctx, result, "neighborSlotsUsed", active) ||
        !wifi_watch_number(ctx, result, "neighborPoolBytes", allocated ? ESP32_MQUICKJS_WIFI_WATCH_NEIGHBOR_SLOTS * sizeof(wifi_watch_neighbor_slot_t) : 0) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "neighborPoolRetired", JS_NewBool(retired)) ||
        !wifi_watch_number(ctx, result, "ingressCapacity", ESP32_MQUICKJS_WIFI_WATCH_INGRESS_CAPACITY) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "ingressDropped", JS_NewUint32(ctx, atomic_load(&s_ingress_dropped))) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "sequenceExhausted", JS_NewBool(exhausted))) {
        JS_PopGCRef(ctx, &result_ref); return JS_EXCEPTION;
    }
    return JS_PopGCRef(ctx, &result_ref);
}

/* Capture options before registering callbacks or allocating native queues.
 * A scoped subscription never enables raw export or widens "all" to other modules. */
static bool wifi_watch_options(JSContext *ctx, int argc, JSValue *argv,
    const char *operation, bool scoped, uint64_t allowed_mask,
    wifi_watch_source_t *out, uint32_t *out_capacity)
{
    static const char *const keys[] = {"events", "capacity", "overflow", "includeRawEventData"};
    static const char *const overflow[] = {"drop-newest"};
    static const char *const all[] = {"all"};
    uint32_t capacity = 16;
    wifi_watch_source_t parsed = {.all = !scoped, .mask = scoped ? allowed_mask : 0};
    JSGCRef options_ref, value_ref, item_ref;
    JSValue *options = JS_PushGCRef(ctx, &options_ref);
    JSValue *value = JS_PushGCRef(ctx, &value_ref);
    JSValue *item = JS_PushGCRef(ctx, &item_ref);
    if (argc > 1) { JS_ThrowTypeError(ctx, "%s() takes at most one options object", operation); goto fail; }
    *options = argc == 1 ? argv[0] : JS_UNDEFINED;
    if (!JS_IsUndefined(*options)) {
        if (!esp32_mquickjs_validate_plain_options(ctx, *options, operation, keys, scoped ? 3 : sizeof(keys) / sizeof(keys[0]))) goto fail;
        *value = JS_GetPropertyStr(ctx, *options, "capacity");
        if (JS_IsException(*value)) goto fail;
        if (!JS_IsUndefined(*value) && !esp32_mquickjs_value_to_bounded_u32(ctx, *value, 1, ESP32_MQUICKJS_WIFI_MAX_WATCH_CAPACITY, &capacity)) goto invalid;
        if (!scoped) {
            *value = JS_GetPropertyStr(ctx, *options, "includeRawEventData");
            if (JS_IsException(*value)) goto fail;
            if (!JS_IsUndefined(*value)) { if (!JS_IsBool(*value)) goto invalid; parsed.raw = *value == JS_TRUE; }
        }
        size_t index;
        *value = JS_GetPropertyStr(ctx, *options, "overflow");
        if (JS_IsException(*value)) goto fail;
        if (!JS_IsUndefined(*value) && !esp32_mquickjs_value_to_enum(ctx, *value, overflow, 1, &index)) goto invalid;
        *value = JS_GetPropertyStr(ctx, *options, "events");
        if (JS_IsException(*value)) goto fail;
        if (!JS_IsUndefined(*value)) {
            if (JS_IsString(ctx, *value)) {
                if (!esp32_mquickjs_value_to_enum(ctx, *value, all, 1, &index)) goto invalid;
            } else {
                if (!JS_IsArray(ctx, *value)) goto invalid;
                *item = JS_GetPropertyStr(ctx, *value, "length");
                uint32_t length;
                if (JS_IsException(*item)) goto fail;
                if (!esp32_mquickjs_value_to_bounded_u32(ctx, *item, 1, WIFI_EVENT_MAX, &length)) goto invalid;
                parsed.all = false;
                parsed.mask = 0;
                for (uint32_t i = 0; i < length; ++i) {
                    *item = JS_GetPropertyUint32(ctx, *value, i);
                    if (JS_IsException(*item)) goto fail;
                    JSCStringBuf buffer; size_t len;
                    if (!JS_IsString(ctx, *item)) goto invalid;
                    const char *text = JS_ToCStringLen(ctx, &len, *item, &buffer);
                    if (text == NULL) goto fail;
                    const wifi_watch_descriptor_t *match = NULL;
                    for (size_t n = 0; n < sizeof(s_descriptors) / sizeof(s_descriptors[0]); ++n)
                        if (strlen(s_descriptors[n].name) == len && memcmp(text, s_descriptors[n].name, len) == 0) { match = &s_descriptors[n]; break; }
                    if (match == NULL || match->id < 0 || match->id >= 64 ||
                        (scoped && !(allowed_mask & (UINT64_C(1) << match->id))) || (parsed.mask & (UINT64_C(1) << match->id)) != 0) goto invalid;
                    parsed.mask |= UINT64_C(1) << match->id;
                }
            }
        }
    }
    *out = parsed;
    *out_capacity = capacity;
    JS_PopGCRef(ctx, &item_ref); JS_PopGCRef(ctx, &value_ref); JS_PopGCRef(ctx, &options_ref);
    return true;
invalid:
    if (!JS_HasException(ctx)) JS_ThrowTypeError(ctx, "invalid %s options", operation);
fail:
    JS_PopGCRef(ctx, &item_ref); JS_PopGCRef(ctx, &value_ref); JS_PopGCRef(ctx, &options_ref);
    return false;
}

static JSValue wifi_watch_open(JSContext *ctx, int argc, JSValue *argv,
    const char *operation, bool scoped, uint64_t allowed_mask)
{
    wifi_watch_source_t parsed;
    uint32_t capacity;
    if (!wifi_watch_options(ctx, argc, argv, operation, scoped, allowed_mask, &parsed, &capacity))
        return JS_EXCEPTION;
    JSGCRef queue_ref;
    JSValue *queue = JS_PushGCRef(ctx, &queue_ref);
    parsed.runtime = esp32_mquickjs_get_active_runtime();
    if (parsed.runtime == NULL) { JS_ThrowInternalError(ctx, "%s requires an active runtime", operation); goto fail; }
    /* Runtime task owns initialization and all source mutations; callback only
     * tries the already published static mutex. Never hold it across JS GC. */
    if (s_watch_mutex == NULL) s_watch_mutex = xSemaphoreCreateMutexStatic(&s_watch_mutex_storage);
    xSemaphoreTake(s_watch_mutex, portMAX_DELAY);
    size_t slot;
    for (slot = 0; slot < ESP32_MQUICKJS_WIFI_MAX_WATCHERS; ++slot) if (s_sources[slot] == NULL) break;
    bool exhausted = s_sequence == WIFI_WATCH_MAX_SEQUENCE;
    xSemaphoreGive(s_watch_mutex);
    if (slot == ESP32_MQUICKJS_WIFI_MAX_WATCHERS || exhausted) { JS_ThrowInternalError(ctx, "%s capacity or sequence exhausted", operation); goto fail; }
    esp_err_t err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) goto native_fail;
    if (s_watch_instance == NULL) {
        err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_watch_callback, NULL, &s_watch_instance);
        if (err != ESP_OK) goto native_fail;
    }
    if (!esp32_mquickjs_register_async_poller(parsed.runtime, wifi_watch_poll, NULL)) { JS_ThrowInternalError(ctx, "%s poller capacity exhausted", operation); goto fail; }
    wifi_watch_source_t *source = esp32_mquickjs_memory_wireless_alloc("wifi", sizeof(*source), ESP32_MQUICKJS_MEMORY_DEFAULT, ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
    if (source == NULL) { JS_ThrowOutOfMemory(ctx); goto fail; }
    *source = parsed;
    *queue = esp32_mquickjs_event_queue_new_wireless("wifi", ctx, parsed.runtime, sizeof(wifi_watch_event_t), capacity,
        ESP32_MQUICKJS_EVENT_QUEUE_DROP_NEWEST, wifi_watch_to_js, wifi_watch_event_drop, wifi_watch_closed, source);
    if (JS_IsException(*queue)) { esp32_mquickjs_memory_payload_free(source); goto fail; }
    source->queue = esp32_mquickjs_event_queue_from_value(ctx, *queue);
    xSemaphoreTake(s_watch_mutex, portMAX_DELAY);
    if (s_ingress == NULL) s_ingress = esp32_mquickjs_memory_queue_create("wifi", ESP32_MQUICKJS_WIFI_WATCH_INGRESS_CAPACITY, sizeof(wifi_watch_event_t), false);
    if (s_ingress == NULL) {
        xSemaphoreGive(s_watch_mutex);
        esp32_mquickjs_event_queue_dispose(ctx, *queue);
        JS_ThrowOutOfMemory(ctx); goto fail;
    }
    if (source->all || (source->mask & (UINT64_C(1) << WIFI_EVENT_STA_NEIGHBOR_REP))) {
        err = wifi_watch_neighbor_open();
        if (err != ESP_OK) {
            xSemaphoreGive(s_watch_mutex);
            esp32_mquickjs_event_queue_dispose(ctx, *queue);
            if (err == ESP_ERR_NO_MEM) { JS_ThrowOutOfMemory(ctx); goto fail; }
            goto native_fail; /* Old queue/receiver still owns the sole pool. */
        }
    }
    source->after_sequence = s_sequence;
    s_sources[slot] = source;
    xSemaphoreGive(s_watch_mutex);
    JSValue result = JS_PopGCRef(ctx, &queue_ref);
    return result;
native_fail:
    esp32_mquickjs_wifi_throw_operation_error(ctx, "WIFI_WATCH_FAILED", operation, err, -1, UINT32_MAX);
fail:
    JS_PopGCRef(ctx, &queue_ref);
    return JS_EXCEPTION;
}

JSValue js_wifi_watch(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    return wifi_watch_open(ctx, argc, argv, "wifi.watch", false, 0);
}

#if CONFIG_ESP_WIFI_RRM_SUPPORT || CONFIG_ESP_WIFI_WNM_SUPPORT || CONFIG_ESP_WIFI_11R_SUPPORT
static uint64_t wifi_roaming_watch_mask(void)
{
    /* Public SDK observations only; none proves BTM acceptance or completed roaming. */
    return (UINT64_C(1) << WIFI_EVENT_STA_START)
        | (UINT64_C(1) << WIFI_EVENT_STA_STOP)
        | (UINT64_C(1) << WIFI_EVENT_STA_CONNECTED)
        | (UINT64_C(1) << WIFI_EVENT_STA_DISCONNECTED)
        | (UINT64_C(1) << WIFI_EVENT_STA_AUTHMODE_CHANGE)
        | (UINT64_C(1) << WIFI_EVENT_STA_BSS_RSSI_LOW)
        | (UINT64_C(1) << WIFI_EVENT_STA_BEACON_TIMEOUT)
        | (UINT64_C(1) << WIFI_EVENT_HOME_CHANNEL_CHANGE)
#if CONFIG_ESP_WIFI_RRM_SUPPORT
        | (UINT64_C(1) << WIFI_EVENT_STA_NEIGHBOR_REP)
#endif
        | (UINT64_C(1) << WIFI_EVENT_STA_BEACON_OFFSET_UNSTABLE);
}

JSValue js_wifi_roaming_watch(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    return wifi_watch_open(ctx, argc, argv, "wifi.roaming.watch", true, wifi_roaming_watch_mask());
}
#endif
#endif
