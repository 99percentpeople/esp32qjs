
static void prepare(unsigned token) {
    assert(!allocations && !timer.fn);
    g_wpa_supp=(struct wpa_supplicant){.rrm={.next_neighbor_rep_token=(u8)token},
        .rrm_ie={WLAN_RRM_CAPS_NEIGHBOR_REPORT},.current_bss=&identities[1]};
    fail_alloc=tx_rc=timer_rc=callbacks=null_callbacks=transmissions=observations=0;
    queue_full=false;
}
static int submit(void) {
    return wpas_rrm_send_neighbor_rep_request(&g_wpa_supp,NULL,0,0,record,&identities[0]);
}
int main(void) {
    for(unsigned token=0;token<256;token++) {
        prepare(token);assert(submit()==0 && sent_token==token && !allocations);
        u8 report[2]={(u8)token,0};
        wpas_rrm_process_neighbor_rep(&g_wpa_supp,report,0);assert(!callbacks && timer.fn);
        report[0]=(u8)(token+1);wpas_rrm_process_neighbor_rep(&g_wpa_supp,report,2);
        assert(!callbacks && timer.fn);
        report[0]=(u8)token;wpas_rrm_process_neighbor_rep(&g_wpa_supp,report,2);
        if(!PATCHED && token==255){assert(!callbacks && timer.fn);fire_timer();assert(null_callbacks==1);}
        else {assert(callbacks==1 && !null_callbacks && !timer.fn);}
        assert(!g_wpa_supp.rrm.notify_neighbor_rep && !g_wpa_supp.rrm.neighbor_rep_cb_ctx);
        wpas_rrm_process_neighbor_rep(&g_wpa_supp,report,2);assert(callbacks==1);
    }
    prepare(1);timer_rc=-1;int rc=submit();assert(!allocations && !timer.fn && transmissions==1);
    if(PATCHED){assert(rc==-ENOMEM && !g_wpa_supp.rrm.notify_neighbor_rep);}
    else {assert(rc==0 && g_wpa_supp.rrm.notify_neighbor_rep==record);assert(submit()==-EBUSY);}
    wpas_rrm_reset(&g_wpa_supp);assert(null_callbacks==(PATCHED?0:1));
    prepare(1);fail_alloc=1;assert(submit()==-ENOMEM && !transmissions && !timer.fn);
    prepare(1);tx_rc=-1;assert(submit()==-ECANCELED && transmissions==1 && !allocations && !timer.fn);
    prepare(1);assert(submit()==0);assert(submit()==-EBUSY && transmissions==1);
    wpas_rrm_reset(&g_wpa_supp);assert(null_callbacks==1 && !timer.fn && g_wpa_supp.rrm.next_neighbor_rep_token==1);
#if PATCHED
    bool attempted;
    prepare(1);g_wpa_supp.current_bss=NULL;
    assert(esp32qjs_rrm_request(record,&identities[0],&attempted)==-2 && !attempted);
    prepare(1);fail_alloc=1;
    assert(esp32qjs_rrm_request(record,&identities[0],&attempted)==-ENOMEM && !attempted);
    prepare(1);timer_rc=-1;
    assert(esp32qjs_rrm_request(record,&identities[0],&attempted)==-ENOMEM && attempted);
    assert(esp32qjs_rrm_query(record,&identities[0])==0);
    prepare(1);assert(esp32qjs_rrm_request(record,&identities[0],&attempted)==0 && attempted);
    assert(packet.length==8 && !memcmp(packet.data+5,profile.ssid,3));
    assert(esp32qjs_rrm_query(record,&identities[0])==1);
    assert(esp32qjs_rrm_cancel(record,&identities[1])==-EBUSY && timer.fn);
    assert(esp32qjs_rrm_cancel(neighbor_report_recvd_cb,&identities[0])==-EBUSY && timer.fn);
    assert(esp32qjs_rrm_request(record,&identities[1],&attempted)==-EBUSY && !attempted);
    assert(esp32qjs_rrm_cancel(record,&identities[0])==0 && !timer.fn && !callbacks);
    assert(esp32qjs_rrm_cancel(record,&identities[0])==0);
    u8 report[2]={sent_token,0};wpas_rrm_process_neighbor_rep(&g_wpa_supp,report,2);assert(!callbacks);
    assert(esp32qjs_rrm_request(record,&identities[0],&attempted)==0);report[0]=sent_token;
    queue_full=true;wpas_rrm_process_neighbor_rep(&g_wpa_supp,report,2);
    assert(callbacks==1 && !observations && esp32qjs_rrm_query(record,&identities[0])==0);
    esp32qjs_rrm_publish_observation(report,2);assert(!observations);
    queue_full=false;esp32qjs_rrm_publish_observation(report,2);assert(observations==1);
    esp32qjs_rrm_publish_observation(NULL,2);esp32qjs_rrm_publish_observation(report,0);assert(observations==1);
    assert(esp32qjs_rrm_request(record,&identities[0],&attempted)==0);fire_timer();
    assert(callbacks==2 && null_callbacks==1 && esp32qjs_rrm_query(record,&identities[0])==0);
    assert(esp32qjs_rrm_query(NULL,&identities[0])==-EINVAL);
    g_wpa_supp.rrm.neighbor_rep_cb_ctx=&identities[1];
    assert(esp32qjs_rrm_query(record,&identities[0])==-EIO);
    assert(esp32qjs_rrm_cancel(record,&identities[0])==-EIO);
#endif
    assert(!allocations);return 0;
}
