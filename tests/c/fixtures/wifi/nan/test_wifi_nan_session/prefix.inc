
#include <assert.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1
#define CONFIG_ESP_WIFI_NAN_SYNC_ENABLE 1
#define CONFIG_ESP_WIFI_NAN_SECURITY 0
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG 101
#define ESP_ERR_INVALID_STATE 102
#define ESP_ERR_NO_MEM 103
#define ESP_ERR_TIMEOUT 104
#define ESP_ERR_NOT_SUPPORTED 105
#define ESP_ERR_INVALID_RESPONSE 106
#define ESP_ERR_NOT_FOUND 107
#define ESP32_MQUICKJS_MEMORY_DEFAULT 0
#define ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL 1
typedef int esp_err_t;
#define ESP_WIFI_NAN_MAX_SVC_SUPPORTED 2
#define ESP_WIFI_NAN_DATAPATH_MAX_PEERS 2
#define ESP_WIFI_MAX_SVC_NAME_LEN 256
#define ESP_WIFI_MAX_FILTER_LEN 256
#define ESP_WIFI_MAX_SVC_SSI_LEN 512
#define ESP_WIFI_MAX_FUP_SSI_LEN 2048
#define NAN_VENDOR_IE_MAX_BODY_LEN 255
#define ESP_WIFI_NAN_MAX_CREDS_PER_SVC 4
#define WIFI_NAN_CSID_NCS_SK_128 1
#define WIFI_NAN_CSID_NCS_PK_PASN_128 7
#define WIFI_NAN_BOOTSTRAP_PIN_CODE_DISPLAY 2
#define WIFI_NAN_BOOTSTRAP_PIN_CODE_KEYPAD 32
#define ESP_WIFI_NAN_NDP_PMK_LEN 32
typedef struct {uint8_t csid;uint8_t use_pmk:1,reserved:7;char passphrase[64];uint8_t pmk[32];}wifi_nan_credential_t;
typedef struct {uint8_t group_data_prot:1,group_mgmt_prot:1,reserved:6;uint8_t num_credentials;wifi_nan_credential_t creds[4];}wifi_nan_discovery_security_params_t;
typedef struct {bool pairing_setup,npk_nik_caching,pairing_verification;uint8_t reserved;uint16_t bootstrapping_methods,comeback_delay;}wifi_nan_pairing_cfg_t;
typedef struct {uint8_t vendor_oui[3];uint16_t body_len;uint8_t *body;}nan_vendor_ie_t;
#define WIFI_EVENT_NAN_SVC_MATCH 1
#define WIFI_EVENT_NAN_REPLIED 2
#define WIFI_EVENT_NAN_RECEIVE 3
typedef struct {uint16_t ghz_2_channels;uint32_t ghz_5_channels;}wifi_scan_channel_bitmap_t;
typedef struct {uint8_t usd_default_channel,n_min,n_max,m_min,m_max;wifi_scan_channel_bitmap_t usd_chan_bitmap;}wifi_nan_usd_config_t;
typedef struct {
    char service_name[256],matching_filter[256];uint8_t *ssi;uint16_t ssi_len;int type;
    wifi_nan_discovery_security_params_t *security_cfg;wifi_nan_pairing_cfg_t *pairing;nan_vendor_ie_t *vendor_ie;
    bool security_reqd,usd_discovery_flag,datapath_reqd,ndp_resp_needed,single_replied_event,single_match_event;
    uint32_t ttl;wifi_nan_usd_config_t usd_publish_config,usd_subscribe_config;
} wifi_nan_publish_cfg_t;
typedef wifi_nan_publish_cfg_t wifi_nan_subscribe_cfg_t;
typedef struct {uint8_t pub_id,peer_mac[6];bool confirm_required;} wifi_nan_datapath_req_t;
typedef struct {uint8_t inst_id,peer_inst_id,peer_mac[6];uint16_t ssi_len;uint8_t *ssi;nan_vendor_ie_t *vendor_ie;}wifi_nan_followup_params_t;
typedef struct {uint8_t subscribe_id,publish_id,pub_if_mac[6],ssi_version;
    bool datapath_reqd,security_reqd,fsd_reqd,fsd_gas,ndpe_support;uint16_t ssi_len;uint8_t ssi[];}wifi_event_nan_svc_match_t;
typedef struct {uint8_t publish_id,subscribe_id,sub_if_mac[6];uint16_t ssi_len;uint8_t ssi[];}wifi_event_nan_replied_t;
typedef struct {uint8_t inst_id,peer_inst_id,peer_if_mac[6];uint16_t ssi_len;uint8_t ssi[];}wifi_event_nan_receive_t;
typedef struct esp32_mquickjs_event_queue esp32_mquickjs_event_queue_t;
typedef struct {uint32_t generation, lease_identity, identity; int kind;} esp32_mquickjs_wifi_radio_operation_t;
typedef struct {
    uint8_t op_channel, master_pref, scan_time; uint16_t warm_up_sec;
    bool disable_random_mac, reset_current_nvs_creds, use_nvs_for_caching, group_mgmt_prot;
} wifi_nan_sync_config_t;
typedef pthread_mutex_t portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED PTHREAD_MUTEX_INITIALIZER
static _Thread_local unsigned lock_depth;
#define portENTER_CRITICAL(p) do {assert(!lock_depth);assert(!pthread_mutex_lock(p));++lock_depth;} while(0)
#define portEXIT_CRITICAL(p) do {assert(lock_depth==1);--lock_depth;assert(!pthread_mutex_unlock(p));} while(0)
