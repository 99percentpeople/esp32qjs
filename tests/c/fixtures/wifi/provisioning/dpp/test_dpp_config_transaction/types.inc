
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#define MAX_SSID_LEN 32
#define MAX_PASSPHRASE_LEN 64
#define PMK_LEN 32
#define ESP_DPP_MAX_CONNECTOR_LEN 512
#define ESP_DPP_MAX_KEY_LEN 128
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_NO_MEM 3
#define os_memchr memchr
#define os_memcpy memcpy
#define os_memcmp memcmp
#define os_strlen strlen
#define os_strnlen strnlen
#define wpa_printf(...) ((void)0)
#define wpa_hexdump_key(...) ((void)0)
#define ESP_DPP_AKM_DPP 1
#define ESP_DPP_AKM_PSK_SAE_DPP 6
typedef int esp_err_t;
enum dpp_akm{DPP_AKM_UNKNOWN,DPP_AKM_DPP,DPP_AKM_PSK,DPP_AKM_SAE,DPP_AKM_PSK_SAE,DPP_AKM_SAE_DPP,DPP_AKM_PSK_SAE_DPP};
struct wpabuf{size_t n;uint8_t bytes[];};
struct dpp_conf{char *connector;struct wpabuf *net_access_key,*c_sign_key;uint64_t net_access_key_expiry;size_t dpp_csign_len;uint8_t curr_chan;enum dpp_akm akm;};
struct dpp_config_store{struct dpp_conf *conf;};
struct dpp_config_obj{char *connector;uint8_t ssid[32],ssid_len;char passphrase[64];uint8_t psk[32];bool psk_set;enum dpp_akm akm;struct wpabuf *c_sign_key;};
struct dpp_authentication{struct wpabuf *net_access_key;uint64_t net_access_key_expiry;unsigned curr_chan;};
