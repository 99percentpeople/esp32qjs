
typedef int wifi_mode_t;
typedef struct{struct{uint32_t addr;}ip,netmask,gw;}esp_netif_ip_info_t;
typedef struct{uint32_t generation,identity;}esp32_mquickjs_wifi_radio_lifecycle_t;
typedef void(*test_worker_fn_t)(void*);
bool esp32_mquickjs_submit_background_worker(test_worker_fn_t,void*);
int64_t esp_timer_get_time(void);
