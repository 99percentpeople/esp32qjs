#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
typedef enum { WIFI_MODE_NULL, WIFI_MODE_STA, WIFI_MODE_AP, WIFI_MODE_APSTA } wifi_mode_t;
typedef enum { WIFI_SECOND_CHAN_NONE, WIFI_SECOND_CHAN_ABOVE, WIFI_SECOND_CHAN_BELOW } wifi_second_chan_t;
typedef enum { WIFI_PS_NONE } wifi_ps_type_t;
typedef struct { int unused; } wifi_init_config_t;
#define WIFI_INIT_CONFIG_DEFAULT() ((wifi_init_config_t){0})
#define WIFI_STORAGE_RAM 1
#define WIFI_IF_AP 1
#define WIFI_COUNTRY_POLICY_MANUAL 1
typedef struct { uint8_t schan, nchan; int policy; uint32_t wifi_5g_channel_mask; } wifi_country_t;
typedef struct { uint8_t primary; } wifi_ap_record_t;
typedef struct { struct { uint8_t channel; } ap; } wifi_config_t;
esp_err_t esp_wifi_init(const wifi_init_config_t *config);
esp_err_t esp_wifi_set_storage(int storage);
esp_err_t esp_wifi_get_mode(wifi_mode_t *mode);
esp_err_t esp_wifi_set_mode(wifi_mode_t mode);
esp_err_t esp_wifi_start(void);
esp_err_t esp_wifi_get_country(wifi_country_t *country);
esp_err_t esp_wifi_get_channel(uint8_t *primary, wifi_second_chan_t *secondary);
esp_err_t esp_wifi_set_channel(uint8_t primary, wifi_second_chan_t secondary);
esp_err_t esp_wifi_get_max_tx_power(int8_t *power);
esp_err_t esp_wifi_get_ps(wifi_ps_type_t *ps);
esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t *ap);
esp_err_t esp_wifi_get_config(int interface, wifi_config_t *config);
esp_err_t esp_wifi_get_promiscuous(bool *enabled);
esp_err_t esp_wifi_set_promiscuous(bool enabled);
