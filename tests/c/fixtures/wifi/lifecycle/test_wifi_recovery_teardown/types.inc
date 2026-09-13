
#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1
#define CONFIG_ESP32_MQUICKJS_WIFI_RADIO 1
#define CONFIG_ESP_WIFI_ENTERPRISE_SUPPORT 1
#define CONFIG_ESP_NETIF_USES_TCPIP_WITH_BSD_API 1
#define CONFIG_LWIP_IPV4 1
#define ESP32_MQUICKJS_FUTURE_SLOT_COUNT 2
#define ESP32_MQUICKJS_FUTURE_MAX_DRIVERS 2
/* VM/queue storage layout boundary; production code owns all transitions. */
typedef int JSContext,JSValue,JSGCRef,esp32_mquickjs_future_token_t;
typedef void *QueueHandle_t,*esp_timer_handle_t;
typedef const void *esp32_mquickjs_resource_key_t;
typedef int esp32_mquickjs_future_runtime_resources_t;
typedef struct {
    void *future_state,*startup_bytecode,*prepare_output,*prepare_output_opaque,*cooperate,*cooperate_opaque;
    int64_t deadline_us,scoped_deadline_us,context_started_us;
    unsigned native_wait_depth,load_root_depth;
    bool littlefs_mounted,repl_enabled,auto_run_startup_script,format_littlefs_on_mount_fail;
    char startup_fs_root[4],load_root[4];
} esp32_mquickjs_runtime_t;
typedef struct esp32_mquickjs_future_driver_state esp32_mquickjs_future_driver_state_t;
typedef struct {
    esp32_mquickjs_cancel_result_t (*cancel)(esp32_mquickjs_future_driver_state_t *);
    esp32_mquickjs_future_poll_t (*poll)(esp32_mquickjs_future_driver_state_t *);
    void (*destroy)(esp32_mquickjs_future_driver_state_t *);
} esp32_mquickjs_future_driver_t;
