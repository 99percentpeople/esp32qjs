
#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1
#define CONFIG_ESP_WIFI_ENTERPRISE_SUPPORT 1
#define CONFIG_ESP_WIFI_MBEDTLS_TLS_CLIENT 1
#define CONFIG_MBEDTLS_CERTIFICATE_BUNDLE 1
#define MALLOC_CAP_8BIT 1
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_SIZE 2
#define ESP_ERR_NOT_SUPPORTED 3
#define ESP_ERR_NO_MEM 4
typedef int esp_err_t,portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
static bool critical;
#define portENTER_CRITICAL(p) do{(void)(p);assert(!critical);critical=true;}while(0)
#define portEXIT_CRITICAL(p) do{(void)(p);assert(critical);critical=false;}while(0)
