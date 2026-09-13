
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
typedef int esp_err_t,JSValue,JSContext;
typedef struct { unsigned char secret[64]; } wifi_config_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE -2
#define JS_EXCEPTION -99
#define WIFI_MODE_AP 2
#define WIFI_STORAGE_RAM 1
static void *s_ap_netif;
static struct { bool acquired; } s_ap_lease;
static bool s_ap_cleanup_pending,pending;
static const char *s_ap_stage;
static struct { const char *cleanup_stage; } state;
static int fail_at,reserves,configures,cleanups,zeroed;
static bool allow_disconnect;
static bool esp32_mquickjs_wifi_parse_ap_config(void *ctx,JSValue options,wifi_config_t *c,bool *allow) { (void)ctx;(void)options;memset(c,0x55,sizeof(*c));*allow=allow_disconnect;return true; }
static int activations,activate_error;
static int esp32_mquickjs_wifi_activate_ap(wifi_config_t *c) {
    assert(allow_disconnect && c->secret[0]==0x55);activations++;
    if(activate_error)state.cleanup_stage="configuration-resume";
    return activate_error;
}
static bool esp32_mquickjs_wifi_radio_accept_ap_config(const wifi_config_t *a,const wifi_config_t *b) { return a && b; }
static int esp32_mquickjs_wifi_radio_reserve_ap(void *lease) { assert(lease);reserves++;if(fail_at==1)return -1;s_ap_lease.acquired=true;return 0; }
static bool esp32_mquickjs_wifi_configuration_pending(void) { return pending; }
static void *esp32_mquickjs_wifi_state(void) { return &state; }
#define esp32_mquickjs_wifi_state() (&state)
static int esp32_mquickjs_wifi_configure_interfaces(int mode,int storage,bool start,void *sta,void *accept_sta,wifi_config_t *ap,bool (*accept)(const wifi_config_t *,const wifi_config_t *),const void *controls,const void *start_controls,bool allow_disconnect) {
    assert(mode==WIFI_MODE_AP && storage==WIFI_STORAGE_RAM && start && !sta && !accept_sta && ap && accept && s_ap_lease.acquired);
    assert(!controls && !start_controls && !allow_disconnect);configures++;if(fail_at==2)return -2;
    if(fail_at==3){pending=true;s_ap_lease.acquired=false;state.cleanup_stage="configuration-commit";return -3;}
    s_ap_netif=(void *)1;return 0;
}
static int share_mode;
static int esp32_mquickjs_wifi_reopen_ap_shared(const wifi_config_t *config,bool *handled) { assert(config);*handled=share_mode!=0;return share_mode==2?-17:0; }
static int wifi_ap_cleanup(void) { assert(!pending);cleanups++;s_ap_lease.acquired=false;s_ap_netif=NULL;return 0; }
static int wifi_ap_result(void *ctx,const wifi_config_t *c) { (void)ctx;assert(c->secret[0]==0x55);return 123; }
static int wifi_ap_error(void *ctx,const char *op,int err) { (void)ctx;assert(!strcmp(op,"wifi.startAP") && err);return -99; }
static int JS_ThrowTypeError(void *ctx,const char *text) { (void)ctx;(void)text;return -99; }
static void esp32_mquickjs_wireless_secure_zero(void *p,size_t n) { memset(p,0,n);zeroed++; }
