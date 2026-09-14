#include <stdatomic.h>
#include <stdio.h>
#define ESPNOW_ADDRESS_BYTES 6
#define ESPNOW_OPERATION_SEND 1
#define ESP_NOW_SEND_SUCCESS 0
#define ESP_NOW_SEND_FAIL 1
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE 259
#define ESP32_MQUICKJS_ESPNOW_TX_SLOT_NONE 65535
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))
typedef int esp_now_send_status_t;
typedef void *TaskHandle_t;
typedef struct { const uint8_t *des_addr; } esp_now_send_info_t;
typedef struct {
    unsigned generation; int operation; uint8_t address[6];
    atomic_bool completed; bool cancelled, timed_out, recovery_failed, tx_completed;
    int err, tx_status; int64_t completed_at_us; size_t payload_length;
    void *runtime; int token;
} esp32_mquickjs_future_driver_state_t;
typedef struct { uint8_t address[6]; } espnow_tx_packet_t;
typedef struct {
    int lock, tx_queue, channel; unsigned generation;
    atomic_uint callbacks_active, active_queued_slot;
    _Atomic(esp32_mquickjs_future_driver_state_t *) active_send;
    espnow_tx_packet_t *tx_packets; unsigned tx_queue_capacity;
    _Atomic(TaskHandle_t) tx_task; void *runtime;
    atomic_bool queued_send_completed;
    atomic_int queued_send_status, queued_send_error;
    atomic_uint send_successes, send_failures, send_unknowns;
    atomic_uint tx_completed_batches, tx_completed_packets, tx_failed_packets;
    atomic_uint tx_settled_packets, tx_submit_rejected_packets, tx_succeeded_packets, tx_unknown_packets;
    atomic_int tx_last_error; bool tx_has_completion; int tx_last_status;
} espnow_session_t;
static espnow_session_t s_espnow_session;
static int64_t esp_timer_get_time(void) { return 123456; }
static int esp32_mquickjs_future_wake(void *runtime,int token) { (void)runtime;(void)token;return 0; }
static void xTaskNotifyGive(TaskHandle_t task) { (void)task; }
static void esp32_mquickjs_notify_activity(void *runtime) { (void)runtime; }
static bool esp32_mquickjs_espnow_tx_queue_complete_active(int *q,bool *batch) { (void)q;*batch=true;return true; }
static JSValue espnow_throw_error(JSContext *ctx,const char *code,int error,const uint8_t *address,int channel) {
    (void)error;(void)address;(void)channel;return JS_ThrowInternalError(ctx,"%s",code);
}
static void espnow_notify_tx_worker(espnow_session_t *session);
