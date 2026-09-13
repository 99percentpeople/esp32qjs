
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#define ESP_ERR_NO_MEM -40
#define MALLOC_CAP_8BIT 1
#define CONFIG_ESP_WIFI_SOFTAP_SUPPORT 1
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG -10
#define ESP_ERR_INVALID_STATE -20
#define ESP_ERR_NOT_SUPPORTED -30
#define WIFI_MODE_NULL 0
#define WIFI_MODE_STA 1
#define WIFI_MODE_AP 2
#define WIFI_MODE_APSTA 3
#define WIFI_STORAGE_FLASH 0
#define WIFI_STORAGE_RAM 1
typedef int esp_err_t,wifi_mode_t,wifi_storage_t,esp32_mquickjs_wifi_radio_lease_t;
typedef int esp32_mquickjs_wifi_radio_config_controls_t;
typedef int esp32_mquickjs_wifi_radio_start_controls_t;
static int start_controls_invalid;
static const int *expected_start_controls;
static int esp32_mquickjs_wifi_radio_validate_start_controls(bool start,const int *controls) {
    (void)start;assert(controls==expected_start_controls);return start_controls_invalid?ESP_ERR_INVALID_ARG:ESP_OK;
}
static int controls_invalid;
static int esp32_mquickjs_wifi_radio_validate_config_controls(int mode,const int *controls) {
    (void)mode;(void)controls;return controls_invalid?ESP_ERR_INVALID_ARG:ESP_OK;
}
typedef struct { unsigned identity; } esp32_mquickjs_wifi_radio_lifecycle_t;
typedef struct { int value; } wifi_config_t;
/* CAPTURE_STRUCTS */
typedef struct { const char *stage;int error; } esp32_mquickjs_wifi_radio_config_result_t;
typedef bool (*esp32_mquickjs_wifi_config_accept_fn)(const wifi_config_t *,const wifi_config_t *);
static bool s_wifi_configuration_cleanup;
static wifi_mode_t s_wifi_configuration_mode;
static esp32_mquickjs_wifi_radio_lifecycle_t s_wifi_lifecycle;
static int s_wifi_application,ap_lease;
static struct { int radio_lease;bool runtime_cleanup_pending;const char *cleanup_stage;int cleanup_error; } s_wifi_state;
static int fail_at,calls[12],cleanup_calls;
static bool prepared_sta,prepared_ap,expect_no_config_inputs,expect_ap_only;
static int activation_mode=WIFI_MODE_APSTA;
static int boundary(int stage) { calls[stage]++;return fail_at==stage?-stage:0; }
static void check_token(const esp32_mquickjs_wifi_radio_lifecycle_t *token) { assert(token==&s_wifi_lifecycle && token->identity==7); }
static int esp32_mquickjs_wifi_radio_validate_ap_config(const wifi_config_t *config) { assert(config);return boundary(1); }
static bool expected_allow_disconnect;
static int disconnect_boundary_error,disconnect_boundaries;
static int wifi_finish_configuration_disconnect(void) {
    assert(s_wifi_configuration_cleanup && s_wifi_lifecycle.identity);disconnect_boundaries++;return disconnect_boundary_error;
}
static int wifi_begin_selected_configuration(esp32_mquickjs_wifi_radio_configuration_selection_t *selection,
    const int *controls,const int *start_controls) {
    assert(selection->allow_disconnect==expected_allow_disconnect);
    /* This fixture isolates the executor. The actual selection/admission is
     * independently exercised by test_wifi_configuration_selection.py. */
    if(!selection->mode_set)selection->mode=selection->access_point_set?WIFI_MODE_AP:WIFI_MODE_STA;
    if(!selection->storage_set)selection->storage=WIFI_STORAGE_RAM;
    if(!selection->start_set)selection->start=false;
    if(selection->activate_ap) {
        assert(!selection->mode_set && !selection->storage_set && !selection->station_set &&
            selection->access_point_set && selection->allow_disconnect && selection->start_set && selection->start);
        assert(!controls && !start_controls);
        /* Inject the native selector's reply. Mode/default resolution itself
         * is exercised through production Radio in the selection fixture. */
        selection->mode=activation_mode;selection->storage=WIFI_STORAGE_FLASH;
    }
    int mode=selection->mode;
    if((mode!=WIFI_MODE_STA && mode!=WIFI_MODE_AP && mode!=WIFI_MODE_APSTA) ||
       (selection->station_set && !(mode&WIFI_MODE_STA)) || (selection->access_point_set && !(mode&WIFI_MODE_AP)) ||
       (selection->start && (mode&WIFI_MODE_AP) && !selection->access_point_set))return ESP_ERR_INVALID_ARG;
    int err=esp32_mquickjs_wifi_radio_validate_config_controls(mode,controls);if(err)return err;
    err=esp32_mquickjs_wifi_radio_validate_start_controls(selection->start,start_controls);if(err)return err;
    err=boundary(2);if(err)return err;
    s_wifi_configuration_cleanup=true;s_wifi_configuration_mode=mode;s_wifi_lifecycle.identity=7;return 0;
}
static int stored_ap_reads,start_begins,running_starts,stored_ap_error;
static bool start_already_running,allocation_failure;
static int live_allocation;
static void *heap_caps_calloc(size_t n,size_t size,int caps) {
    assert(caps==1 && n==1 && size==sizeof(wifi_config_t));
    if(allocation_failure)return NULL;live_allocation++;return calloc(n,size);
}
static void heap_caps_free(void *p) {
    assert(live_allocation==1);for(size_t i=0;i<sizeof(wifi_config_t);i++)assert(!((unsigned char *)p)[i]);
    live_allocation--;free(p);
}
static void esp32_mquickjs_wireless_secure_zero(void *p,size_t n) { memset(p,0,n); }
static int wifi_begin_start_configuration(esp32_mquickjs_wifi_radio_configuration_selection_t *s,bool *already) {
    assert(s->start_only);start_begins++;*already=start_already_running;
    if(!s->mode_set)s->mode=WIFI_MODE_APSTA;
    if(!s->storage_set)s->storage=WIFI_STORAGE_FLASH;
    s->start=true;
    if(!*already){s_wifi_configuration_cleanup=true;s_wifi_configuration_mode=s->mode;s_wifi_lifecycle.identity=7;}
    return 0;
}
static int wifi_start_existing_running(const esp32_mquickjs_wifi_radio_configuration_selection_t *s) {
    assert(start_already_running && s->mode==WIFI_MODE_APSTA);running_starts++;return 0;
}
static int esp32_mquickjs_wifi_radio_copy_stopped_ap_configuration(const esp32_mquickjs_wifi_radio_lifecycle_t *t,wifi_config_t *config) {
    check_token(t);assert(calls[10]==1 && !calls[11]);stored_ap_reads++;config->value=77;return stored_ap_error;
}
static int wifi_finish_configuration_cleanup(void) {
    assert(s_wifi_configuration_cleanup && s_wifi_lifecycle.identity);cleanup_calls++;
    s_wifi_lifecycle.identity=0;s_wifi_configuration_cleanup=false;s_wifi_configuration_mode=WIFI_MODE_NULL;
    s_wifi_state.runtime_cleanup_pending=false;s_wifi_state.cleanup_stage=NULL;s_wifi_state.cleanup_error=0;
    return 0;
}
static void esp32_mquickjs_wifi_radio_release(int *lease) { assert(s_wifi_lifecycle.identity);*lease=0; }
static int esp32_mquickjs_wifi_radio_quiesce_lifecycle(const esp32_mquickjs_wifi_radio_lifecycle_t *t) { check_token(t);return boundary(3); }
static int esp32_mquickjs_wifi_ap_retire_for_configuration(const esp32_mquickjs_wifi_radio_lifecycle_t *t) { check_token(t);return boundary(4); }
static int wifi_cleanup_helper(bool finish) {
    assert(!finish && s_wifi_lifecycle.identity);int err=boundary(5);
    if(err){s_wifi_state.cleanup_stage="netif-detach";return err;}
    memset(&s_wifi_state,0,sizeof(s_wifi_state));return 0;
}
static int esp32_mquickjs_wifi_radio_initialize_lifecycle(const esp32_mquickjs_wifi_radio_lifecycle_t *t) { check_token(t);return boundary(6); }
static int esp32_mquickjs_wifi_prepare_for_configuration(const esp32_mquickjs_wifi_radio_lifecycle_t *t) {
    check_token(t);assert(!s_wifi_state.runtime_cleanup_pending && !s_wifi_state.cleanup_stage);int err=boundary(7);if(!err)prepared_sta=true;return err;
}
static int esp32_mquickjs_wifi_ap_prepare_for_configuration(const esp32_mquickjs_wifi_radio_lifecycle_t *t) {
    check_token(t);int err=boundary(8);if(!err)prepared_ap=true;return err;
}
static int *esp32_mquickjs_wifi_ap_configuration_slot(const esp32_mquickjs_wifi_radio_lifecycle_t *t) {
    check_token(t);assert(prepared_ap);return boundary(9)?NULL:&ap_lease;
}
static int esp32_mquickjs_wifi_radio_configure_lifecycle(const esp32_mquickjs_wifi_radio_lifecycle_t *t,
    int mode,int storage,wifi_config_t *sta,esp32_mquickjs_wifi_config_accept_fn accept_sta,
    wifi_config_t *ap,esp32_mquickjs_wifi_config_accept_fn accept_ap,const esp32_mquickjs_wifi_radio_config_controls_t *controls,esp32_mquickjs_wifi_radio_config_result_t *result) {
    check_token(t);assert(mode>0 && storage>=0 && result);(void)controls;
    assert(!sta || accept_sta);assert(!ap || accept_ap);
    if(expect_no_config_inputs)assert(!sta && !ap);
    if(expect_ap_only)assert(!sta && !accept_sta && ap && accept_ap && storage==WIFI_STORAGE_FLASH);
    int err=boundary(10);result->stage="sdk-config";result->error=err;return err;
}
static int esp32_mquickjs_wifi_radio_resume_lifecycle(esp32_mquickjs_wifi_radio_lifecycle_t *t,
    int mode,bool start,int *app,int *sta,int *ap,const int *controls) {
    assert(controls==expected_start_controls);
    check_token(t);
    assert((app!=NULL)==(start && (mode&WIFI_MODE_STA)));
    assert((sta!=NULL)==(start && (mode&WIFI_MODE_STA)));
    assert((ap!=NULL)==(start && (mode&WIFI_MODE_AP)));
    if(sta)assert(prepared_sta && sta==&s_wifi_state.radio_lease && app==&s_wifi_application);
    if(ap)assert(prepared_ap && ap==&ap_lease);
    int err=boundary(11);if(err)return err;
    if(app)*app=1;if(sta)*sta=2;if(ap)*ap=3;t->identity=0;return 0;
}
static bool accept_config(const wifi_config_t *a,const wifi_config_t *b) { return a && b; }
static bool esp32_mquickjs_wifi_radio_accept_station_config(const wifi_config_t *a,const wifi_config_t *b) {return accept_config(a,b);}
static bool esp32_mquickjs_wifi_radio_accept_ap_config(const wifi_config_t *a,const wifi_config_t *b) {return accept_config(a,b);}
static void reset_fixture(void) {
    assert(!s_wifi_configuration_cleanup && !s_wifi_lifecycle.identity);
    memset(&s_wifi_state,0,sizeof(s_wifi_state));memset(calls,0,sizeof(calls));
    s_wifi_application=ap_lease=0;prepared_sta=prepared_ap=false;cleanup_calls=0;
}
