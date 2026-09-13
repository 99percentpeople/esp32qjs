
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1
#define CONFIG_SOC_WIFI_HE_SUPPORT 1
#define CONFIG_IDF_TARGET_ESP32C5 1
#define IRAM_ATTR
#define DRAM_ATTR
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define WIFI_EVENT_BTWT_SETUP 33
#define WIFI_EVENT_BTWT_TEARDOWN 34
static esp_err_t esp32_mquickjs_wifi_btwt_event_post(int id,const void *data,size_t size) {
    (void)id;(void)data;(void)size;assert(0);return ESP_ERR_INVALID_ARG;
}
#define WIFI_EVENT_ITWT_PROBE 30
#define WIFI_EVENT_ITWT_SETUP 28
#define WIFI_EVENT_ITWT_SUSPEND 31
#define WIFI_EVENT_ITWT_TEARDOWN 29
#define WIFI_EVENT ((const char *)19)
#define ESP_EVENT_DECLARE_BASE(name)
#define ESP32QJS_WIFI_RADIO_CONTROL_EVENT ((const char *)20)
typedef struct {esp_err_t status;uint8_t flow_id_bitmap;uint32_t actual_suspend_time_ms[8];} wifi_event_sta_itwt_suspend_t;
/* Setup routing has a separate production fixture. */
static esp_err_t esp32_mquickjs_wifi_twt_setup_result_post(const void *data,size_t size) {
    (void)data;(void)size;assert(0);return ESP_ERR_INVALID_ARG;
}
static esp_err_t esp32_mquickjs_wifi_twt_setup_result_teardown_post(const void *data,size_t size) {
    (void)data;(void)size;assert(0);return ESP_ERR_INVALID_ARG;
}
typedef enum {ITWT_PROBE_FAIL,ITWT_PROBE_SUCCESS,ITWT_PROBE_TIMEOUT,ITWT_PROBE_STA_DISCONNECTED} wifi_itwt_probe_status_t;
typedef struct {wifi_itwt_probe_status_t status;uint8_t reason;} wifi_event_sta_itwt_probe_t;
typedef unsigned portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
static unsigned locked;
#define portENTER_CRITICAL_SAFE(lock) do {assert(!locked);locked=1;} while(0)
#define portEXIT_CRITICAL_SAFE(lock) do {assert(locked==1);locked=0;} while(0)
static esp_err_t esp_event_post(const char *,int32_t,const void *,size_t,unsigned);
