
int main(void) {
    struct wps_context context={.rf_band_cb=callback};
    struct wps_data data={.wps=&context};
    struct wpabuf *(*builders[])(struct wps_data *)={wps_build_m1,wps_build_m2,wps_build_m2d};
    const int bands[]={0,-1,8,WPS_RF_24GHZ,WPS_RF_50GHZ,WPS_RF_24GHZ|WPS_RF_50GHZ};
    for(unsigned b=0;b<3;++b)for(unsigned i=0;i<6;++i) {
        rf_result=bands[i];callbacks=allocations=crypto=0;encoded=0;allocation_failure=false;
        struct wpabuf *msg=builders[b](&data);
        assert(callbacks==1);
        if(i<3)assert(!msg && !allocations && !crypto && !encoded);
        else {assert(msg==&message && allocations==1 && encoded==bands[i]);wpabuf_free(msg);}
    }
    context.rf_band_cb=NULL;callbacks=allocations=crypto=0;
    assert(!wps_build_m1(&data) && !wps_build_m2(&data) && !wps_build_m2d(&data) && !callbacks && !allocations && !crypto);
    context.rf_band_cb=callback;rf_result=WPS_RF_50GHZ;allocation_failure=true;
    for(unsigned b=0;b<3;++b)assert(!builders[b](&data));
    return 0;
}
