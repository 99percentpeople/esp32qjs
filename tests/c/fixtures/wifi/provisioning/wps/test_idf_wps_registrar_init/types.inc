
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
typedef uint8_t u8;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define WPA_PROTO_WPA 1
#define WPA_PROTO_RSN 2
#define WPA_KEY_MGMT_PSK 1
#define WPA_KEY_MGMT_PSK_SHA256 2
#define WPA_KEY_MGMT_SAE 4
#define WPA_CIPHER_TKIP 1
#define WPA_CIPHER_CCMP 2
#define WPS_AUTH_OPEN 1
#define WPS_AUTH_WPAPSK 2
#define WPS_AUTH_WPA2PSK 0x20
#define WPS_ENCR_NONE 1
#define WPS_ENCR_TKIP 4
#define WPS_ENCR_AES 8
#define ETH_ALEN 6
#define WPS_UUID_LEN 16
#define WPS_STATE_CONFIGURED 2
#define MSG_DEBUG 0
#define MSG_ERROR 0
#define os_memcpy memcpy
#define os_memset memset
#define os_strlen strlen
#define os_strnlen strnlen
#define os_free free
#define wpa_printf(...) ((void)0)
static bool oom;
static unsigned clears,freed;
static void forced_memzero(void *p,size_t n){assert(p);volatile uint8_t *v=p;while(n--)*v++=0;clears++;}
static void bin_clear_free(void *p,size_t n){if(p){forced_memzero(p,n);for(size_t i=0;i<n;i++)assert(!((uint8_t*)p)[i]);free(p);freed++;}}
static void *os_malloc(size_t n){return oom?NULL:malloc(n);}
static void *os_zalloc(size_t n){return oom?NULL:calloc(1,n);}
struct wps_credential {u8 ssid[32],key[64];size_t ssid_len,key_len;uint16_t auth_type,encr_type;};
struct wps_device_data {int unused,rf_bands;};
struct wps_context {u8 ssid[32],uuid[16];size_t ssid_len;struct wps_device_data dev;void *registrar;void (*event_cb)(void);int (*rf_band_cb)(void);void *cb_ctx;int wps_state,ap_setup_locked,ap;};
struct wps_data {struct wps_context *wps;struct wps_credential *use_cred;int registrar;u8 *dev_password;};
struct hostapd_ssid {u8 ssid[32];size_t ssid_len;char *wpa_passphrase;};
struct wpa_auth_config {int wpa,wpa_key_mgmt,wpa_pairwise,rsn_pairwise;};
struct hostapd_bss_config {struct hostapd_ssid ssid;};
struct wpa_authenticator {struct wpa_auth_config conf;};
struct hostapd_data {struct hostapd_bss_config *conf;struct wpa_authenticator *wpa_auth;struct wps_context *wps;void *eapol_auth;};
struct wps_registrar_config {int (*set_ie_cb)(void);void (*reg_success_cb)(void);void *cb_ctx;int disable_auto_conf;};
