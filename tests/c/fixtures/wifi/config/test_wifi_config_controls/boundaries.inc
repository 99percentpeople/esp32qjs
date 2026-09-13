
static struct {
    int lock;bool driver_owned,storage_configured,started,stop_required,promiscuous_claimed;
    const char *fault_stage,*cleanup_stage;int driver_state;
    wifi_storage_t storage;wifi_mode_t effective_mode;unsigned wake_locks;
    struct { unsigned identity; } operation,leases[WIFI_RADIO_MAX_LEASES];
    esp32_mquickjs_wifi_radio_lifecycle_t lifecycle;
    esp32_mquickjs_wifi_radio_config_result_t configuration;
} s_radio;
static wifi_mode_t native_mode;
static wifi_storage_t native_storage;
static wifi_country_t native_country;
static wifi_protocols_t native_protocols[2];
static wifi_bandwidths_t native_bandwidths[2];
static wifi_config_t native_config[2];
static wifi_ps_type_t native_ps;
static wifi_band_mode_t native_band;
static esp32_mquickjs_wifi_radio_config_result_t result;
static int depth,critical,calls,writes,fail_at,rollback_fail,alloc_live;
static size_t allocated_size;
static bool allocation_failure,corrupt_ps,corrupted,reenable_pmf;
static int pmf_error,pmf_calls;
static const char *rollback_failed_stage;
static void wifi_radio_operation_lock(void) { assert(!depth && !critical);depth=1; }
static void wifi_radio_operation_unlock(void) { assert(depth && !critical);depth=0; }
#define taskENTER_CRITICAL(p) do { (void)(p);assert(!critical);critical=1; } while(0)
#define taskEXIT_CRITICAL(p) do { (void)(p);assert(critical);critical=0; } while(0)
static int step(bool write) {
    assert(depth && !critical);calls++;writes+=write;
    if(calls==fail_at)return -77;
    if(result.rollback_attempted && rollback_fail && --rollback_fail==0) {
        rollback_failed_stage=result.rollback_stage;return -88;
    }
    return 0;
}
static void *allocate(size_t n,size_t size) {
    assert(!alloc_live && depth && !critical);if(allocation_failure)return NULL;
    allocated_size=n*size;void *p=calloc(n,size);assert(p);alloc_live++;return p;
}
static void release(void *p) {
    assert(alloc_live==1);for(size_t i=0;i<allocated_size;i++)assert(((uint8_t *)p)[i]==0);
    alloc_live--;free(p);
}
static int wifi_radio_record_fault(const char *stage,int error) { s_radio.fault_stage=stage;return error; }
static int wifi_radio_cleanup_fault(const char *stage,int error) { s_radio.cleanup_stage=stage;return error; }
static int esp_wifi_get_mode(wifi_mode_t *v) { int e=step(false);if(!e)*v=native_mode;return e; }
static int esp_wifi_set_mode(wifi_mode_t v) { int e=step(true);native_mode=v;return e; }
static int esp_wifi_set_storage(wifi_storage_t v) { int e=step(true);native_storage=v;return e; }
static int esp_wifi_get_config(wifi_interface_t i,wifi_config_t *v) { int e=step(false);if(!e)*v=native_config[i];return e; }
static int esp_wifi_set_config(wifi_interface_t i,wifi_config_t *v) {
    int e=step(true);native_config[i]=*v;
    if(reenable_pmf) {
        wifi_pmf_config_t *pmf=i==WIFI_IF_STA ? &native_config[i].sta.pmf_cfg : &native_config[i].ap.pmf_cfg;
        pmf->capable=true;
    }
    return e;
}
static int esp_wifi_disable_pmf_config(wifi_interface_t i) {
    assert(!s_radio.started);++pmf_calls;
    wifi_pmf_config_t *pmf=i==WIFI_IF_STA ? &native_config[i].sta.pmf_cfg : &native_config[i].ap.pmf_cfg;
    pmf->capable=pmf->required=false;int err=step(true);return err ? err : pmf_error;
}
static int esp_wifi_get_country(wifi_country_t *v) { int e=step(false);if(!e)*v=native_country;return e; }
static int esp_wifi_set_country(const wifi_country_t *v) { int e=step(true);int8_t power=native_country.max_tx_power;native_country=*v;native_country.max_tx_power=power;return e; }
static int esp_wifi_set_country_code(const char *code,bool automatic) {
    int e=step(true);native_country.cc[0]=code[0];native_country.cc[1]=code[1];native_country.cc[2]=' ';
    native_country.policy=automatic?WIFI_COUNTRY_POLICY_AUTO:WIFI_COUNTRY_POLICY_MANUAL;
    native_country.schan=1;native_country.nchan=13;return e;
}
static int esp_wifi_get_band_mode(wifi_band_mode_t *v) { int e=step(false);if(!e)*v=native_band;return e; }
static int esp_wifi_get_protocols(wifi_interface_t i,wifi_protocols_t *v) { int e=step(false);if(!e)*v=native_protocols[i];return e; }
static int esp_wifi_set_protocols(wifi_interface_t i,wifi_protocols_t *v) { int e=step(true);native_protocols[i]=*v;return e; }
static int esp_wifi_get_bandwidths(wifi_interface_t i,wifi_bandwidths_t *v) { int e=step(false);if(!e)*v=native_bandwidths[i];return e; }
static int esp_wifi_set_bandwidths(wifi_interface_t i,wifi_bandwidths_t *v) { int e=step(true);native_bandwidths[i]=*v;return e; }
static int esp_wifi_get_protocol(wifi_interface_t i,uint8_t *v) { int e=step(false);if(!e)*v=native_protocols[i].ghz_2g;return e; }
static int esp_wifi_set_protocol(wifi_interface_t i,uint8_t v) { int e=step(true);native_protocols[i].ghz_2g=v;return e; }
static int esp_wifi_get_bandwidth(wifi_interface_t i,wifi_bandwidth_t *v) { int e=step(false);if(!e)*v=native_bandwidths[i].ghz_2g;return e; }
static int esp_wifi_set_bandwidth(wifi_interface_t i,wifi_bandwidth_t v) { int e=step(true);native_bandwidths[i].ghz_2g=v;return e; }
static int esp_wifi_set_ps(wifi_ps_type_t v) { int e=step(true);native_ps=v;return e; }
static int esp_wifi_get_ps(wifi_ps_type_t *v) {
    int e=step(false);if(!e)*v=native_ps;
    if(!e && corrupt_ps && !corrupted && !strcmp(result.stage,"power-save-readback")) {
        *v=WIFI_PS_NONE;corrupted=true;
    }
    return e;
}
#define calloc allocate
#define free release
