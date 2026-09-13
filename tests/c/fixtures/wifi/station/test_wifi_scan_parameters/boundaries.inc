
#define WIFI_SCAN_PARAMS_DEFAULT_CONFIG() {.scan_time={.active={.min=0,.max=120},.passive=360},.home_chan_dwell_time=30}
static wifi_scan_default_params_t native_scan=WIFI_SCAN_PARAMS_DEFAULT_CONFIG();
static bool scan_partial_failure,scan_corrupt;
static unsigned scan_writes,scan_resets;
static int esp_wifi_get_scan_parameters(wifi_scan_default_params_t *out) {
    assert(s_radio.started && (s_radio.effective_mode&WIFI_MODE_STA));
    int error=sdk_step(false);
    if(!error){*out=native_scan;if(scan_corrupt)out->home_chan_dwell_time=1;}
    return error;
}
static int esp_wifi_set_scan_parameters(const wifi_scan_default_params_t *input) {
    assert(s_radio.started && (s_radio.effective_mode&WIFI_MODE_STA));
    ++scan_writes;scan_resets+=input==NULL;
    if(input) {
        /* Exactly the fixed SDK's 44-byte read, so ASAN catches an unpadded
         * public 16-byte argument. Extra command bytes must be deterministic. */
        unsigned char command[44];memcpy(command,input,sizeof(command));
        for(unsigned i=sizeof(*input);i<sizeof(command);++i)assert(command[i]==0);
    }
    int error=sdk_step(true);
    if(!error || scan_partial_failure) {
        native_scan=(wifi_scan_default_params_t)WIFI_SCAN_PARAMS_DEFAULT_CONFIG();
        if(input){native_scan=*input;
            if(!native_scan.scan_time.active.max)native_scan.scan_time.active.max=120;
            if(!native_scan.scan_time.passive)native_scan.scan_time.passive=360;
            if(!native_scan.home_chan_dwell_time)native_scan.home_chan_dwell_time=30;}
    }
    return error;
}
