#include "esp32_mquickjs_wifi_csi_rx_native.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI_CSI
#include "esp32_mquickjs_wifi_csi_rx_span.h"
#include "esp_attr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdatomic.h>
#include <string.h>

/* SDK constructor ABI verified against all three pinned libpp archives. */
extern void wdev_csi_rx_process(const void *samples, uint32_t sample_bytes,
    const void *rx, const void *header, uint32_t reported_bytes, uint32_t invalid_word);

#if CONFIG_IDF_TARGET_ESP32C5
#define CSI_RX_METADATA_BYTES 64U
#define CSI_RX_PREFIX_BYTES 56U
#else
#define CSI_RX_METADATA_BYTES 48U
#define CSI_RX_PREFIX_BYTES 44U
#endif
_Static_assert(sizeof(wifi_pkt_rx_ctrl_t) == CSI_RX_METADATA_BYTES, "unreviewed CSI RX metadata layout");

typedef struct wifi_csi_rx_scope {
    struct wifi_csi_rx_scope *previous;
    const void *samples;
    const void *header;
    size_t readable_bytes;
    uint32_t sample_bytes;
    uint32_t reported_bytes;
    bool consumed;
} wifi_csi_rx_scope_t;

typedef struct {
    TaskHandle_t task;
    esp32_mquickjs_wifi_csi_rx_span_t pending;
    wifi_csi_rx_scope_t *scope;
} wifi_csi_rx_lane_t;

#define CSI_RX_LANES 4U
static portMUX_TYPE s_csi_rx_lock = portMUX_INITIALIZER_UNLOCKED;
static wifi_csi_rx_lane_t s_csi_rx_lanes[CSI_RX_LANES];
static _Atomic bool s_csi_rx_enabled;

static IRAM_ATTR wifi_csi_rx_lane_t *wifi_csi_rx_lane(TaskHandle_t task, bool create)
{
    wifi_csi_rx_lane_t *empty = NULL;
    for (size_t i = 0; i < CSI_RX_LANES; ++i) {
        if (s_csi_rx_lanes[i].task == task && task != NULL) return &s_csi_rx_lanes[i];
        if (s_csi_rx_lanes[i].scope == NULL && !s_csi_rx_lanes[i].pending.valid) empty = &s_csi_rx_lanes[i];
    }
    if (create && task != NULL && empty != NULL) {
        empty->task = task;
        return empty;
    }
    return NULL;
}

size_t esp32_mquickjs_wifi_csi_rx_native_control_bytes(void)
{
    return sizeof(s_csi_rx_lock) + sizeof(s_csi_rx_lanes) + sizeof(s_csi_rx_enabled);
}

void esp32_mquickjs_wifi_csi_rx_native_enable(bool enabled)
{
    taskENTER_CRITICAL(&s_csi_rx_lock);
    atomic_store_explicit(&s_csi_rx_enabled, enabled, memory_order_release);
    for (size_t i = 0; i < CSI_RX_LANES; ++i) s_csi_rx_lanes[i].pending.valid = false;
    taskEXIT_CRITICAL(&s_csi_rx_lock);
}

/* Only first-prefix copies at reviewed RX call sites are redirected here.
 * memcpy executes unchanged, outside the lock, before publishing its receipt. */
void *IRAM_ATTR esp32qjs_wifi_csi_rx_begin_copy(void *destination, const void *source, size_t length)
{
    void *result = memcpy(destination, source, length);
    if (!atomic_load_explicit(&s_csi_rx_enabled, memory_order_acquire) || xPortInIsrContext()) return result;
    TaskHandle_t task = xTaskGetCurrentTaskHandle();
    taskENTER_CRITICAL(&s_csi_rx_lock);
    wifi_csi_rx_lane_t *lane = wifi_csi_rx_lane(task, true);
    if (lane != NULL) {
        lane->pending.valid = false;
        if (length == CSI_RX_PREFIX_BYTES)
            esp32_mquickjs_wifi_csi_rx_span_begin(&lane->pending, destination, CSI_RX_METADATA_BYTES, length);
    }
    taskEXIT_CRITICAL(&s_csi_rx_lock);
    return result;
}

void *IRAM_ATTR esp32qjs_wifi_csi_rx_append_copy(void *destination, const void *source, size_t length)
{
    void *result = memcpy(destination, source, length);
    if (!atomic_load_explicit(&s_csi_rx_enabled, memory_order_acquire) || xPortInIsrContext()) return result;
    TaskHandle_t task = xTaskGetCurrentTaskHandle();
    taskENTER_CRITICAL(&s_csi_rx_lock);
    wifi_csi_rx_lane_t *lane = wifi_csi_rx_lane(task, false);
    if (lane != NULL) esp32_mquickjs_wifi_csi_rx_span_append(&lane->pending, destination, length);
    taskEXIT_CRITICAL(&s_csi_rx_lock);
    return result;
}

void IRAM_ATTR esp32qjs_wifi_csi_rx_copied(const void *samples, uint32_t sample_bytes,
    const void *rx, const void *header, uint32_t reported_bytes, uint32_t invalid_word)
{
    if (!atomic_load_explicit(&s_csi_rx_enabled, memory_order_acquire) || xPortInIsrContext()) {
        wdev_csi_rx_process(samples, sample_bytes, rx, header, reported_bytes, invalid_word);
        return;
    }
    TaskHandle_t task = xTaskGetCurrentTaskHandle();
    wifi_csi_rx_scope_t scope = {
        .samples = samples, .sample_bytes = sample_bytes,
        .header = header, .reported_bytes = reported_bytes,
    };
    taskENTER_CRITICAL(&s_csi_rx_lock);
    wifi_csi_rx_lane_t *lane = wifi_csi_rx_lane(task, true);
    if (lane != NULL) {
        (void)esp32_mquickjs_wifi_csi_rx_span_finish(&lane->pending, rx, header, &scope.readable_bytes);
        lane->pending.valid = false;
        scope.previous = lane->scope;
        lane->scope = &scope;
    }
    taskEXIT_CRITICAL(&s_csi_rx_lock);
    wdev_csi_rx_process(samples, sample_bytes, rx, header, reported_bytes, invalid_word);
    taskENTER_CRITICAL(&s_csi_rx_lock);
    if (lane != NULL && lane->scope == &scope) lane->scope = scope.previous;
    taskEXIT_CRITICAL(&s_csi_rx_lock);
}

bool esp32_mquickjs_wifi_csi_rx_native_take(const wifi_csi_info_t *info,
    esp32_mquickjs_wifi_csi_rx_packet_t *packet)
{
    if (packet == NULL) return false;
    *packet = (esp32_mquickjs_wifi_csi_rx_packet_t){0};
    if (info == NULL || !atomic_load_explicit(&s_csi_rx_enabled, memory_order_acquire) || xPortInIsrContext()) return false;
    TaskHandle_t task = xTaskGetCurrentTaskHandle();
    taskENTER_CRITICAL(&s_csi_rx_lock);
    wifi_csi_rx_lane_t *lane = wifi_csi_rx_lane(task, false);
    wifi_csi_rx_scope_t *scope = lane != NULL ? lane->scope : NULL;
    if (scope != NULL && !scope->consumed) {
        scope->consumed = true;
        if (scope->samples == info->buf && scope->sample_bytes == info->len &&
            scope->header == info->hdr && scope->reported_bytes == info->payload_len &&
            scope->readable_bytes != 0) {
            packet->bytes = scope->header;
            packet->readable_bytes = scope->readable_bytes;
        }
    }
    taskEXIT_CRITICAL(&s_csi_rx_lock);
    return packet->bytes != NULL;
}
#endif
