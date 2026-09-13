
int main(void) {
    esp_netif_api_msg_t msg={.esp_netif=&object};
    for(int mode=0;mode<3;mode++) {
        create_at_same_address();memset(timers,0,sizeof(timers));emitted=foreign_fired=0;
        stack.up=mode==2;
        assert(esp_netif_start_ip_lost_timer(&object,false)==ESP_OK);
        sys_timeout(120000,foreign_timer,&foreign_arg);
        assert(pending()==1+CONFIG_ESP_NETIF_LOST_IP_TIMER_ENABLE);
        if(mode==0) {
            /* Direct SDK destroy, followed by allocator reuse of the exact address. */
            assert(esp_netif_destroy_api(&msg)==ESP_OK && freed);
            create_at_same_address();
        } else {
            /* STOP must also retire the timer before the already-down early return. */
            assert(esp_netif_stop_api(&msg)==(mode==1?ESP_ERR_ESP_NETIF_IF_NOT_READY:ESP_OK));
            assert(!stack.up);
            if(PATCHED)assert(!object.timer_running);
        }
        assert(pending()==1+(!PATCHED && CONFIG_ESP_NETIF_LOST_IP_TIMER_ENABLE));
        advance();
        /* Original SDK emits a stale event against the new object; patched code
         * removes the exact timer without cancelling an unrelated callback. */
        assert(emitted==(!PATCHED && CONFIG_ESP_NETIF_LOST_IP_TIMER_ENABLE));
        assert(foreign_fired==1 && pending()==0);
        assert(esp_netif_destroy_api(&msg)==ESP_OK && freed && !pending());
    }
    return 0;
}
