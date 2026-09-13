
#define ESP_FAIL (-1)
#define ESP_ERR_NO_MEM 0x101
#undef ESP_ERR_INVALID_ARG
#undef ESP_ERR_INVALID_STATE
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_WIFI_NOT_INIT 0x3001
#define WIFI_PROTOCOL_LR 8
#define WIFI_PROTOCOL_11B 1
#define WIFI_PROTOCOL_11G 2
#define WIFI_PROTOCOL_11N 4
#define WIFI_PROTOCOL_11A 16
#define WIFI_PROTOCOL_11AC 32
#define WIFI_PROTOCOL_11AX 64
typedef struct { uint16_t ghz_2g,ghz_5g; } wifi_protocols_t;
int esp_wifi_get_protocol(wifi_interface_t,unsigned char *);
int esp_wifi_get_protocols(wifi_interface_t,wifi_protocols_t *);
