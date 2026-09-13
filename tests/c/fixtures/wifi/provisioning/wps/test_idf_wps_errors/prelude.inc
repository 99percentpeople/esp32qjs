
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
typedef uint8_t u8;typedef uint16_t u16;typedef int esp_err_t;typedef int ETS_STATUS;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_INVALID_SIZE 0x104
#define ESP_ERR_INVALID_ARG 0x102
#define WIFI_IF_STA 0
#define ETH_ALEN 6
#define ETH_P_EAPOL 0x888e
#define WIFI_APPIE_WPS_PR 1
#define WIFI_APPIE_WPS_AR 2
#define WSC_ID_ENROLLEE_LEN 8
#define WSC_ID_ENROLLEE "enrollee"
#define WPS_UUID_LEN 16
#define WPS_STATUS_DISABLE 0
#define WPS_STATUS_PENDING 1
#define WPS_STATUS_SCANNING 2
#define WAIT_START 1
#define WPS_CONNECT_TIMEOUT_SECS 30
#define WPS_SCAN_RETRY_TIMEOUT_SECS 2
#define DEV_PW_PUSHBUTTON 4
#define DEV_PW_DEFAULT 0
#define WPS_REQ_ENROLLEE 1
#define WPA_KEY_MGMT_SAE 8
#define WIFI_EVENT 1
#define WIFI_EVENT_STA_WPS_ER_PIN 2
#define WIFI_EVENT_STA_WPS_ER_FAILED 3
#define WIFI_EVENT_STA_WPS_ER_PBC_OVERLAP 4
#define OS_BLOCK 0
#define ELOOP_ALL_CTX ((void *)-1)
#define wpa_printf(...) ((void)0)
#define os_memcpy memcpy
#define os_memset memset
static void forced_memzero(void *p,size_t n) {memset(p,0,n);}
struct wpabuf {size_t used;uint8_t data[64];};
static size_t wpabuf_len(const struct wpabuf *b) {return b->used;}
static const void *wpabuf_head(const struct wpabuf *b) {return b->data;}
static void wpabuf_free(struct wpabuf *b) {(void)b;}
static void wpabuf_clear_free(struct wpabuf *b) {if(b)memset(b->data,0,sizeof(b->data));}
static struct wpabuf probe,extra,assoc;
static int probe_oom,resize_error,assoc_oom,appie_error;
struct wps_device_data {unsigned config_methods;};
struct wps_context {struct wps_device_data dev;uint8_t uuid[16];unsigned config_methods;void *cred_cb;struct wpabuf *dh_privkey;};
struct wps_config {struct wps_context *wps;uint8_t pin[9];};
struct wps_data {struct wps_context *wps;uint8_t dev_password[8];};
struct wps_sm {
    struct wps_device_data *dev;struct wps_context *wps_ctx;struct wps_data *wps;
    uint8_t ownaddr[6],identity[16],uuid[16],bssid[6];size_t identity_len;
    struct {struct wpabuf *in_buf;} wsc_frag;
    struct {uint8_t ssid[32];size_t ssid_len;} creds[3];
    unsigned discover_ssid_cnt,channel;bool wps_pbc_overlap;int state;
};
struct wps_funcs {void *wps_parse_scan_result,*wifi_station_wps_start,*wps_sm_rx_eapol,*wps_start_pending;};
struct wps_sm_funcs {void *wps_sm_notify_deauth;};
typedef struct {uint8_t pin_code[8];} wifi_event_sta_wps_er_pin_t;
typedef struct {struct {uint8_t bssid[6],ssid[32],bssid_set,channel,failure_retry_cnt;} sta;} wifi_config_t;
struct wps_scan_ie {uint8_t *rsn;};
struct wpa_ie_data {unsigned key_mgmt;};
