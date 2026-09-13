
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#define CONFIG_IDF_TARGET_ESP32C5 1
typedef uint8_t u8;
typedef uint16_t u16;
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_INVALID_SIZE 0x104
#define ESP_ERR_TIMEOUT 0x107
#define ESP_ERR_WIFI_STATE 0x3006
#define ESP_ERR_NOT_FINISHED 0x10a
#define SSID_MAX_LEN 32
#define ETH_ALEN 6
#define MAX_CRED_COUNT 3
#define WPS_OWNER_NONE 0
#define MSG_ERROR 0
#define wpa_printf(...) ((void)0)
#define WIFI_EVENT 7
#define OS_BLOCK UINT32_MAX
#define ELOOP_ALL_CTX ((void *)-1)
#define WPS_STATUS_DISABLE 0
#define WPS_STATUS_PENDING 1
#define WAIT_START 1
#define WPA_MESG 2
#define WIFI_APPIE_WPS_PR 1
#define WIFI_APPIE_WPS_AR 2
enum {WIFI_EVENT_STA_WPS_ER_SUCCESS=10,WIFI_EVENT_STA_WPS_ER_FAILED,
    WIFI_EVENT_STA_WPS_ER_TIMEOUT,WIFI_EVENT_STA_WPS_ER_PIN,WIFI_EVENT_STA_WPS_ER_PBC_OVERLAP};
typedef enum {WPS_FAIL_REASON_NORMAL,WPS_FAIL_REASON_RECV_M2D,WPS_FAIL_REASON_RECV_DEAUTH,
    WPS_FAIL_REASON_MAX} wifi_event_sta_wps_fail_reason_t;
typedef void (*eloop_timeout_handler)(void *,void *);
typedef struct {struct {uint8_t ssid[32],password[64];} sta;} wifi_config_t;
