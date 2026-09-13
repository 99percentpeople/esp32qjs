
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#define ESP_OK 0
#define ESP_ERR_INVALID_RESPONSE 12
#define WPS_RF_24GHZ 1
#define WPS_RF_50GHZ 2
#define WPS_RF_60GHZ 4
typedef int wifi_band_t;
typedef int wifi_band_mode_t;
typedef int esp_err_t;
enum {WIFI_BAND_2G=1,WIFI_BAND_5G=2};
enum {WIFI_BAND_MODE_2G_ONLY=1,WIFI_BAND_MODE_5G_ONLY=2,WIFI_BAND_MODE_AUTO=3};
static int band,band_mode,sdk_error,reads;
static int esp_wifi_get_band(wifi_band_t *out){++reads;*out=band;return sdk_error;}
static int esp_wifi_get_band_mode(wifi_band_mode_t *out){++reads;*out=band_mode;return sdk_error;}
#define wpa_printf(...) ((void)0)
