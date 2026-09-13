
typedef struct {
 uint8_t ssid[32],ssid_len,password[64],password_len;
 char connector[512];uint16_t connector_len;
 uint8_t net_access_key[128];uint16_t net_access_key_len;
 uint8_t c_sign_key[128];uint16_t c_sign_key_len;
 uint64_t net_access_key_expiry;uint8_t curr_chan,akm;
} esp_dpp_config_data_t;
