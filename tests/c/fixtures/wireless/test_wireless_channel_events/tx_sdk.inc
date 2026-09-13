
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
