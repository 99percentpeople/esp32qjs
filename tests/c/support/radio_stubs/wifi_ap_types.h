#pragma once
/* Fixed SDK fff9895c82 public AP declarations from the recorded inventory.
 * Host representation exercises validation, not ESP32 target ABI. */
#include <stdbool.h>
#include <stdint.h>
typedef enum
{
  WIFI_AUTH_OPEN = 0,
  WIFI_AUTH_WEP,
  WIFI_AUTH_WPA_PSK,
  WIFI_AUTH_WPA2_PSK,
  WIFI_AUTH_WPA_WPA2_PSK,
  WIFI_AUTH_ENTERPRISE,
  WIFI_AUTH_WPA2_ENTERPRISE = WIFI_AUTH_ENTERPRISE,
  WIFI_AUTH_WPA3_PSK,
  WIFI_AUTH_WPA2_WPA3_PSK,
  WIFI_AUTH_WAPI_PSK,
  WIFI_AUTH_OWE,
  WIFI_AUTH_WPA3_ENT_192,
  WIFI_AUTH_DUMMY_1,
  WIFI_AUTH_DUMMY_2,
  WIFI_AUTH_DPP,
  WIFI_AUTH_WPA3_ENTERPRISE,
  WIFI_AUTH_WPA2_WPA3_ENTERPRISE,
  WIFI_AUTH_WPA_ENTERPRISE,
  WIFI_AUTH_UNKNOWN,
  WIFI_AUTH_MAX
} wifi_auth_mode_t;

typedef enum
{
  WIFI_CIPHER_TYPE_NONE = 0,
  WIFI_CIPHER_TYPE_WEP40,
  WIFI_CIPHER_TYPE_WEP104,
  WIFI_CIPHER_TYPE_TKIP,
  WIFI_CIPHER_TYPE_CCMP,
  WIFI_CIPHER_TYPE_TKIP_CCMP,
  WIFI_CIPHER_TYPE_AES_CMAC128,
  WIFI_CIPHER_TYPE_SMS4,
  WIFI_CIPHER_TYPE_GCMP,
  WIFI_CIPHER_TYPE_GCMP256,
  WIFI_CIPHER_TYPE_AES_GMAC128,
  WIFI_CIPHER_TYPE_AES_GMAC256,
  WIFI_CIPHER_TYPE_UNKNOWN
} wifi_cipher_type_t;

typedef struct
{
  bool capable;
  bool required;
} wifi_pmf_config_t;

typedef enum
{
  WPA3_SAE_PWE_UNSPECIFIED,
  WPA3_SAE_PWE_HUNT_AND_PECK,
  WPA3_SAE_PWE_HASH_TO_ELEMENT,
  WPA3_SAE_PWE_BOTH
} wifi_sae_pwe_method_t;

typedef struct
{
  uint16_t period;
  bool protected_keep_alive;
} wifi_bss_max_idle_config_t;

typedef struct
{
  uint8_t ssid[32];
  uint8_t password[64];
  uint8_t ssid_len;
  uint8_t channel;
  wifi_auth_mode_t authmode;
  uint8_t ssid_hidden;
  uint8_t max_connection;
  uint16_t beacon_interval;
  uint8_t csa_count;
  uint8_t dtim_period;
  wifi_cipher_type_t pairwise_cipher;
  bool ftm_responder;
  wifi_pmf_config_t pmf_cfg;
  wifi_sae_pwe_method_t sae_pwe_h2e;
  uint8_t transition_disable : 1;
  uint8_t sae_ext : 1;
  uint8_t wpa3_compatible_mode : 1;
  uint8_t reserved : 5;
  wifi_bss_max_idle_config_t bss_max_idle_cfg;
  uint16_t gtk_rekey_interval;
} wifi_ap_config_t;
