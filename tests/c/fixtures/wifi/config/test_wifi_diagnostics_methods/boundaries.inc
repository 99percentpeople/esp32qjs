
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_WIFI_NOT_INIT 3
#define WIFI_STATIS_BUFFER 1U
#define WIFI_STATIS_RXTX 2U
#define WIFI_STATIS_HW 4U
#define WIFI_STATIS_DIAG 8U
#define WIFI_STATIS_PS 16U
#define WIFI_STATIS_ALL (-1)
static unsigned locks, sdk_calls, locked_calls;
static uint32_t seen_mask;
static int sdk_error;
static struct {
    bool driver_owned, storage_configured, restart_required;
    struct {unsigned identity;} lifecycle, operation;
    const char *fault_stage, *cleanup_stage;
    esp32_mquickjs_wifi_radio_driver_state_t driver_state;
} s_radio;
static void wifi_radio_operation_lock(void) {assert(!locks);locks++;locked_calls++;}
static void wifi_radio_operation_unlock(void) {assert(locks==1);locks--;}
static int esp_wifi_statis_dump(uint32_t mask) {assert(locks==1);sdk_calls++;seen_mask=mask;return sdk_error;}
