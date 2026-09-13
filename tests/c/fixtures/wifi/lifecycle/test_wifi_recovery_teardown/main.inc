
int main(void) {
    for(int kind=0;kind<3;++kind) for(unsigned point=0;point<6;++point) {
        setup_restart();original.kind=kind;recovery_mode=WIFI_MODE_STA;
        recovery_admitted=false;recovery_owner_pending=true;recovery_shutdown_calls=0;
        hold_future=hold_worker=hold_raw=hold_enterprise=hold_smartconfig=true;vm_frees=root_deletes=recovery_disposals=0;
        smartconfig_prepares=0;
        action_prepares=raw_prepares=ftm_prepares=0;
        esp32_mquickjs_future_driver_state_t public_state={.operation=original,.timeout_ms=1000};
        assert(!esp32_mquickjs_wifi_recovery_begin(&public_state.recovery,&original,false));
        bool complete=false;
        for(unsigned i=0;i<point;++i)
            assert(!esp32_mquickjs_wifi_recovery_step(&public_state.recovery,&complete) && !complete);
        future_slot_t slots[2]={
            {.allocated=true,.driver_active=true,.state=FUTURE_STATE_PENDING,.driver=&recovery_driver,.driver_state=&public_state},
            {.allocated=true,.driver_active=true,.state=FUTURE_STATE_PENDING,.driver=&pending_driver},
        };
        future_driver_chunk_t drivers={.count=1,.entries={{.retained=true}}};
        future_runtime_t future={.slots=slots,.driver_count=1,.drivers=&drivers};registry_storage=&drivers;
        esp32_mquickjs_runtime_t runtime={.future_state=&future};JSContext ctx=0;s_active_runtime=&runtime;
        assert(!esp32_mquickjs_destroy_internal(&ctx,&runtime,false));
        assert(future.shutting_down && recovery_disposals==1 && !temporary && !slots[0].allocated);
        /* Before repair: core returned after Future prepare, never reaching this cleanup. */
        assert(!running && recovery_shutdown_calls && s_wifi_configuration_cleanup && s_wifi_lifecycle.identity);
        assert(action_prepares==1 && raw_prepares==1 && ftm_prepares==1);
        assert(smartconfig_prepares==1); /* Native cleanup advances while Future is pending. */
        assert(!vm_frees && !root_deletes && slots[1].allocated && s_active_runtime==&runtime);
        unsigned stopped=stop_calls;
        assert(!esp32_mquickjs_destroy_internal(&ctx,&runtime,false));
        assert(stop_calls==stopped && recovery_owner_pending && !vm_frees);
        hold_worker=false;
        assert(!esp32_mquickjs_destroy_internal(&ctx,&runtime,false));
        assert(!recovery_owner_pending && s_wifi_configuration_cleanup && !vm_frees);
        /* Owner retirement permits central suffix, even while another Future waits. */
        assert(!esp32_mquickjs_destroy_internal(&ctx,&runtime,false));
        assert(!s_wifi_configuration_cleanup && !s_wifi_lifecycle.identity && !vm_frees);
        assert(!phase_calls[2] && !phase_calls[5] && !phase_calls[9]); /* no restoration */
        hold_future=false;
        assert(!esp32_mquickjs_destroy_internal(&ctx,&runtime,false));
        assert(!vm_frees && root_deletes==1 && !future.driver_count && !slots[1].allocated);
        hold_raw=false;
        assert(!esp32_mquickjs_destroy_internal(&ctx,&runtime,false) && !vm_frees && enterprise_prepares);
        hold_enterprise=false;
        assert(!esp32_mquickjs_destroy_internal(&ctx,&runtime,false) && !vm_frees && smartconfig_prepares);
        hold_smartconfig=false;
        assert(esp32_mquickjs_destroy_internal(&ctx,&runtime,false));
        assert(vm_frees==1 && !s_active_runtime && !runtime.future_state && stop_calls==stopped);
    }
    for(int kind=0;kind<3;++kind) for(int failure=1;failure<=4;++failure) {
        setup_restart();original.kind=kind;recovery_mode=WIFI_MODE_APSTA;
        s_ap_netif=(void *)1;s_ap_lease=(esp32_mquickjs_wifi_radio_lease_t){9,3,true};
        recovery_admitted=false;recovery_owner_pending=true;recovery_shutdown_calls=0;
        hold_future=hold_raw=false;hold_worker=true;vm_frees=0;
        esp32_mquickjs_future_driver_state_t public_state={.operation=original,.timeout_ms=1000};
        assert(!esp32_mquickjs_wifi_recovery_begin(&public_state.recovery,&original,false));
        /* Inject an already completed original-owner boundary for final lifecycle
         * failure; otherwise hold it until the cleanup reaches physical drain. */
        if(failure==4)recovery_owner_pending=false;
        future_slot_t slots[2]={{.allocated=true,.driver_active=true,.state=FUTURE_STATE_PENDING,
            .driver=&recovery_driver,.driver_state=&public_state}};
        future_runtime_t future={.slots=slots};
        esp32_mquickjs_runtime_t runtime={.future_state=&future};JSContext ctx=0;s_active_runtime=&runtime;
        fail_at=failure;
        assert(!esp32_mquickjs_destroy_internal(&ctx,&runtime,false));
        assert(!vm_frees && s_wifi_configuration_cleanup && s_wifi_lifecycle.identity);
        assert(s_wifi_state.cleanup_error==-failure && !temporary);
        assert(!esp32_mquickjs_destroy_internal(&ctx,&runtime,false));
        assert(!vm_frees && s_active_runtime==&runtime && !phase_calls[2]);
        unsigned stopped=stop_calls,ap_retired=ap_calls,sta_retired=sta_calls;
        fail_at=0;hold_worker=false;
        bool destroyed=esp32_mquickjs_destroy_internal(&ctx,&runtime,false);
        if(!destroyed)destroyed=esp32_mquickjs_destroy_internal(&ctx,&runtime,false);
        assert(destroyed && vm_frees==1 && !s_wifi_configuration_cleanup && !s_wifi_lifecycle.identity);
        if(failure>1)assert(stop_calls==stopped);
        if(failure>2)assert(ap_calls==ap_retired);
        if(failure>3)assert(sta_calls==sta_retired);
    }
    /* No admitted recovery is not permission to initiate STOP or recovery. */
    setup_restart();recovery_admitted=false;recovery_owner_pending=false;
    assert(esp32_mquickjs_prepare_wifi_recovery_runtime_destroy());
    assert(!stop_calls && !owner_releases && running);
    assert(!wifi_begin_configuration_cleanup(false));
    assert(esp32_mquickjs_prepare_wifi_recovery_runtime_destroy());
    assert(!stop_calls && running && s_wifi_configuration_cleanup);
    return 0;
}
