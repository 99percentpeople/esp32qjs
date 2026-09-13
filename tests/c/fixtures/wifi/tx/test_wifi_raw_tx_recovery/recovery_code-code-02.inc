typedef struct {
        uint32_t generation;esp32_mquickjs_wifi_raw_tx_token_t token;
        bool operation_active,submit_returned,control_busy,native_terminated;
    } esp32_mquickjs_wifi_raw_tx_broker_status_t;
    static esp32_mquickjs_wifi_raw_tx_broker_status_t raw_native;
    static unsigned raw_quiesces;static bool raw_unregistered;static int raw_unregister_error;
    