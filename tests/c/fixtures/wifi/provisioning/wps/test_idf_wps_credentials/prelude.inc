
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
typedef uint8_t u8;
typedef uint16_t u16;
#define SSID_MAX_LEN 32
#define ETH_ALEN 6
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_ARG 0x102
#define WIFI_IF_STA 0
#define WPS_AUTH_WPAPSK 2
#define WPS_ENCR_AES 8
#define WIFI_AUTH_WPA_PSK 2
#define os_memcpy memcpy
#define os_memset memset
#define MAX_CRED_COUNT 3
