
int main(void) {
    int total=1;
    for(int nth=0;nth<=total;nth++) {
        s_ble.advertiser.generation=4;
        s_ble.connections[0]=(ble_connection_slot_t){.open=true,.allocated=true,.incoming_pending=true,
            .generation=7,.incoming_advertiser_generation=4,.conn_handle=11};
        s_ble.connections[1]=(ble_connection_slot_t){.open=true,.allocated=true,.conn_handle=12};
        ble_advertiser_event_t event={.generation=4,.connection_generation=7};
        calls=retired=terminated=0;fail_at=nth;
        JSValue result=ble_advertiser_event_to_js(NULL,&event,&s_ble.advertiser);
        assert(!roots && !s_ble.connections[0].incoming_pending);
        if(!nth) {
            total=calls;assert(result!=JS_EXCEPTION && !retired && !terminated);
            ble_release_advertiser(&s_ble);assert(!terminated);
        } else {
            assert(result==JS_EXCEPTION && terminated==1 && last_handle==11);
            assert(retired==(nth==1 ? 0 : 1));
            assert(s_ble.connections[0].release_on_disconnect);
            ble_advertiser_event_drop(&event,&s_ble.advertiser);assert(terminated==1);
        }
        assert(s_ble.connections[1].open && !s_ble.connections[1].release_on_disconnect);
    }
}
