
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <sys/queue.h>
#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1
#define CONFIG_ESP_WIFI_ENTERPRISE_SUPPORT 1
#define USE_WPA2_TASK 1
#define EAP_PEER_METHOD 1
#define TRUE 1
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM -2
#define ESP_ERR_INVALID_STATE -3
#define ESP_ERR_INVALID_ARG -4
#define OS_BLOCK UINT32_MAX
#define DATA_MUTEX_TAKE() os_mutex_lock(s_wpa2_data_lock)
#define DATA_MUTEX_GIVE() os_mutex_unlock(s_wpa2_data_lock)
#define WIFI_IF_STA 0
#define WPA2_TASK_STACK_SIZE 4096
#define WPA2_TASK_PRIORITY 7
#define MSG_ERROR 1
#define MSG_DEBUG 2
#define MSG_INFO 3
#define MSG_WARNING 4
#define wpa_printf(...) ((void)0)
typedef uint8_t u8;
typedef uint32_t u32;
typedef int esp_err_t;
typedef enum {WPA2_STATE_DISABLED,WPA2_STATE_ENABLED} wpa2_state_t;
enum {SIG_WPA2_START,SIG_WPA2_RX,SIG_WPA2_TASK_DEL,SIG_WPA2_MAX};
enum {WPA2_ENT_EAP_STATE_NOT_START};
typedef struct {uint32_t sig,par;} ETSEvent;
struct eap_sm {uint32_t wpa2_sig_cnt[SIG_WPA2_MAX];int finish_state,current_identifier;
    u8 ownaddr[6];void *ssl_ctx;int workaround;bool eap_process_started;};
static struct eap_sm *gEapSm;
static void *s_wpa2_data_lock;
static bool g_wpa_config_changed;
struct wpa_sm {int (*wpa_sm_eap_disable)(void);};
static struct wpa_sm gWpaSm;
static void eap_peer_sm_deinit(void);
int wpa2_post(uint32_t,uint32_t);
static void eap_start_eapol(void *a,void *b){(void)a;(void)b;}
