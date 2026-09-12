#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
typedef enum { WIFI_MODE_NULL, WIFI_MODE_STA, WIFI_MODE_AP, WIFI_MODE_APSTA } wifi_mode_t;
typedef enum { WIFI_SECOND_CHAN_NONE, WIFI_SECOND_CHAN_ABOVE, WIFI_SECOND_CHAN_BELOW } wifi_second_chan_t;
typedef enum { WIFI_PS_NONE, WIFI_PS_MIN_MODEM, WIFI_PS_MAX_MODEM } wifi_ps_type_t;
typedef struct { int unused; } wifi_init_config_t;
#define WIFI_INIT_CONFIG_DEFAULT() ((wifi_init_config_t){0})
typedef int wifi_storage_t;
#define WIFI_STORAGE_FLASH 0
#define WIFI_STORAGE_RAM 1
#define WIFI_IF_AP 1
#define WIFI_COUNTRY_POLICY_MANUAL 1
typedef struct { uint8_t schan, nchan; int policy; uint32_t wifi_5g_channel_mask; } wifi_country_t;
typedef struct { uint8_t primary; } wifi_ap_record_t;
#include "wifi_ap_types.h"
typedef struct { wifi_ap_config_t ap; } wifi_config_t;
esp_err_t esp_wifi_set_config(int interface,wifi_config_t *config);
esp_err_t esp_wifi_init(const wifi_init_config_t *config);
esp_err_t esp_wifi_set_storage(int storage);
esp_err_t esp_wifi_get_mode(wifi_mode_t *mode);
esp_err_t esp_wifi_set_mode(wifi_mode_t mode);
esp_err_t esp_wifi_start(void);
esp_err_t esp_wifi_stop(void);
esp_err_t esp_wifi_deinit(void);
esp_err_t esp_wifi_get_country(wifi_country_t *country);
esp_err_t esp_wifi_get_channel(uint8_t *primary, wifi_second_chan_t *secondary);
esp_err_t esp_wifi_set_channel(uint8_t primary, wifi_second_chan_t secondary);
esp_err_t esp_wifi_get_max_tx_power(int8_t *power);
esp_err_t esp_wifi_get_ps(wifi_ps_type_t *ps);
esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t *ap);
esp_err_t esp_wifi_get_config(int interface, wifi_config_t *config);
esp_err_t esp_wifi_get_promiscuous(bool *enabled);
esp_err_t esp_wifi_set_promiscuous(bool enabled);
typedef enum { WIFI_PKT_MGMT, WIFI_PKT_CTRL, WIFI_PKT_DATA, WIFI_PKT_MISC } wifi_promiscuous_pkt_type_t;
typedef void (*wifi_promiscuous_cb_t)(void *, wifi_promiscuous_pkt_type_t);
typedef struct { uint32_t filter_mask; } wifi_promiscuous_filter_t;
esp_err_t esp_wifi_set_promiscuous_rx_cb(wifi_promiscuous_cb_t callback);
esp_err_t esp_wifi_get_promiscuous_filter(wifi_promiscuous_filter_t *filter);
esp_err_t esp_wifi_set_promiscuous_filter(const wifi_promiscuous_filter_t *filter);
esp_err_t esp_wifi_get_promiscuous_ctrl_filter(wifi_promiscuous_filter_t *filter);
esp_err_t esp_wifi_set_promiscuous_ctrl_filter(const wifi_promiscuous_filter_t *filter);

esp_err_t esp_wifi_set_ps(wifi_ps_type_t ps);
esp_err_t esp_wifi_set_max_tx_power(int8_t power);

/* Pinned SDK public promiscuous masks, mirrored for the SDK boundary fixture. */
#define WIFI_PROMIS_CTRL_FILTER_MASK_ACK (1<<29)
#define WIFI_PROMIS_CTRL_FILTER_MASK_ALL (0xFF800000)
#define WIFI_PROMIS_CTRL_FILTER_MASK_BA (1<<25)
#define WIFI_PROMIS_CTRL_FILTER_MASK_BAR (1<<24)
#define WIFI_PROMIS_CTRL_FILTER_MASK_CFEND (1<<30)
#define WIFI_PROMIS_CTRL_FILTER_MASK_CFENDACK (1<<31)
#define WIFI_PROMIS_CTRL_FILTER_MASK_CTS (1<<28)
#define WIFI_PROMIS_CTRL_FILTER_MASK_PSPOLL (1<<26)
#define WIFI_PROMIS_CTRL_FILTER_MASK_RTS (1<<27)
#define WIFI_PROMIS_CTRL_FILTER_MASK_WRAPPER (1<<23)
#define WIFI_PROMIS_FILTER_MASK_ALL (0xFFFFFFFF)
#define WIFI_PROMIS_FILTER_MASK_CTRL (1<<1)
#define WIFI_PROMIS_FILTER_MASK_DATA (1<<2)
#define WIFI_PROMIS_FILTER_MASK_DATA_AMPDU (1<<5)
#define WIFI_PROMIS_FILTER_MASK_DATA_MPDU (1<<4)
#define WIFI_PROMIS_FILTER_MASK_FCSFAIL (1<<6)
#define WIFI_PROMIS_FILTER_MASK_MGMT (1)
#define WIFI_PROMIS_FILTER_MASK_MISC (1<<3)

#define ESP_WIFI_CONNECTIONLESS_INTERVAL_DEFAULT_MODE 0
esp_err_t esp_wifi_connectionless_module_set_wake_interval(uint16_t interval);
