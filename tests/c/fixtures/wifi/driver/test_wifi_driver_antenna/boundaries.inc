
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_WIFI_NOT_INIT 4
#define ESP_ERR_INVALID_RESPONSE 6
static unsigned locks,reads,ant_reads,gpio_reads;
static int sdk_error;
static struct {
    bool driver_owned,storage_configured,restart_required;
    struct {unsigned identity;} lifecycle,operation;
    const char *fault_stage,*cleanup_stage;
    esp32_mquickjs_wifi_radio_driver_state_t driver_state;
    /* Observations must not mutate unrelated owners, wake or routing state. */
    unsigned lease_identity,wake_locks;
    bool promiscuous_claimed;
} s_radio;
static esp_phy_ant_config_t stored_ant;
static esp_phy_ant_gpio_config_t stored_gpio;
static void wifi_radio_operation_lock(void) {assert(!locks);++locks;}
static void wifi_radio_operation_unlock(void) {assert(locks==1);--locks;}
static int esp_phy_get_ant(esp_phy_ant_config_t *out) {
    assert(locks==1);++reads;++ant_reads;*out=stored_ant;return sdk_error;
}
static int esp_phy_get_ant_gpio(esp_phy_ant_gpio_config_t *out) {
    assert(locks==1);++reads;++gpio_reads;*out=stored_gpio;return sdk_error;
}
