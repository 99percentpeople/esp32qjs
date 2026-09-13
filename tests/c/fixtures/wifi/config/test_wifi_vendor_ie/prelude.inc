
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG -1
#define ESP_ERR_INVALID_STATE -2
#define ESP_ERR_NOT_SUPPORTED -3
#define ESP_ERR_WIFI_NOT_INIT -4
#define ESP_ERR_NO_MEM -5
#define WIFI_RADIO_MAX_LEASES 12
#define WIFI_VENDOR_IE_ELEMENT_ID 221
/* These isolated Vendor/EAP cases contain no NAN or Mesh native owner. */
#define WIFI_RADIO_NAN_PENDING false
#define WIFI_RADIO_MESH_PENDING false
typedef int esp_err_t;
