
int main(void) {
    esp32_mquickjs_wifi_antenna_snapshot_t out,zero={0};const char *stage;
    reset();assert(esp32_mquickjs_wifi_radio_read_antenna(false,NULL,&stage)==ESP_ERR_INVALID_ARG && !reads);
    memset(&out,0xff,sizeof(out));
    assert(esp32_mquickjs_wifi_radio_read_antenna(false,&out,NULL)==ESP_ERR_INVALID_ARG && !memcmp(&out,&zero,sizeof(out)) && !reads);
    for(unsigned gpio=0;gpio<2;++gpio) {
        reset();unsigned char before[sizeof(s_radio)];memcpy(before,&s_radio,sizeof(s_radio));
        assert(esp32_mquickjs_wifi_radio_read_antenna(gpio,&out,&stage)==ESP_OK && reads==1);
        assert(ant_reads==!gpio && gpio_reads==gpio && !memcmp(before,&s_radio,sizeof(s_radio)));
        if(gpio) {
            for(unsigned i=0;i<4;++i)assert(out.gpio.gpio_cfg[i].gpio_select==i%2 && out.gpio.gpio_cfg[i].gpio_num==127-i);
            memset(&stored_gpio,0,sizeof(stored_gpio));assert(out.gpio.gpio_cfg[0].gpio_num==127);
        } else {
            assert(out.config.enabled_ant0==15 && out.config.enabled_ant1==14);
            stored_ant.enabled_ant0=0;assert(out.config.enabled_ant0==15);
        }
        s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STOPPED;
        assert(esp32_mquickjs_wifi_radio_read_antenna(gpio,&out,&stage)==ESP_OK);
        sdk_error=77;memset(&out,0xff,sizeof(out));
        assert(esp32_mquickjs_wifi_radio_read_antenna(gpio,&out,&stage)==77 && !memcmp(&out,&zero,sizeof(out)));
        assert(!strcmp(stage,gpio ? "antenna-gpio" : "antenna"));
        for(unsigned bad=0;bad<8;++bad) {
            reset();switch(bad) {
                case 0:s_radio.driver_owned=false;break;
                case 1:s_radio.storage_configured=false;break;
                case 2:s_radio.lifecycle.identity=1;break;
                case 3:s_radio.operation.identity=1;break;
                case 4:s_radio.fault_stage="test";break;
                case 5:s_radio.cleanup_stage="test";break;
                case 6:s_radio.restart_required=true;break;
                case 7:s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTING;break;
            }
            memcpy(before,&s_radio,sizeof(s_radio));memset(&out,0xff,sizeof(out));
            assert(esp32_mquickjs_wifi_radio_read_antenna(gpio,&out,&stage)==(bad ? ESP_ERR_INVALID_STATE : ESP_ERR_WIFI_NOT_INIT));
            assert(!reads && !memcmp(&out,&zero,sizeof(out)) && !strcmp(stage,"admission") && !memcmp(before,&s_radio,sizeof(s_radio)));
        }
        for(unsigned state=ESP32_MQUICKJS_WIFI_RADIO_UNINITIALIZED;state<=ESP32_MQUICKJS_WIFI_RADIO_CLEANUP_PENDING;++state) {
            if(state==ESP32_MQUICKJS_WIFI_RADIO_STOPPED || state==ESP32_MQUICKJS_WIFI_RADIO_STARTED)continue;
            reset();s_radio.driver_state=state;memset(&out,0xff,sizeof(out));
            assert(esp32_mquickjs_wifi_radio_read_antenna(gpio,&out,&stage)==ESP_ERR_INVALID_STATE);
            assert(!reads && !memcmp(&out,&zero,sizeof(out)));
        }
    }
    for(unsigned bad=0;bad<7;++bad) {
        reset();switch(bad) {
            case 0:stored_ant.rx_ant_mode=ESP_PHY_ANT_MODE_MAX;break;
            case 1:stored_ant.tx_ant_mode=ESP_PHY_ANT_MODE_MAX;break;
            case 2:stored_ant.rx_ant_default=ESP_PHY_ANT_MAX;break;
            case 3:stored_ant.rx_ant_mode=-1;break;
            case 4:stored_ant.tx_ant_mode=-1;break;
            case 5:stored_ant.rx_ant_default=-1;break;
            case 6:stored_ant.rx_ant_mode=ESP_PHY_ANT_MODE_ANT0;break;
        }
        assert(esp32_mquickjs_wifi_radio_read_antenna(false,&out,&stage)==ESP_ERR_INVALID_RESPONSE);
        assert(!memcmp(&out,&zero,sizeof(out)) && !strcmp(stage,"decode"));
    }
    for(unsigned rx=0;rx<3;++rx)for(unsigned tx=0;tx<3;++tx)for(unsigned def=0;def<2;++def)for(unsigned selector=0;selector<16;++selector) {
        if(tx==ESP_PHY_ANT_MODE_AUTO && rx!=ESP_PHY_ANT_MODE_AUTO)continue;
        reset();stored_ant.rx_ant_mode=rx;stored_ant.tx_ant_mode=tx;stored_ant.rx_ant_default=def;
        stored_ant.enabled_ant0=selector;stored_ant.enabled_ant1=15-selector;
        assert(esp32_mquickjs_wifi_radio_read_antenna(false,&out,&stage)==ESP_OK);
        assert(out.config.rx_ant_mode==rx && out.config.tx_ant_mode==tx && out.config.rx_ant_default==def);
        assert(out.config.enabled_ant0==selector && out.config.enabled_ant1==15-selector);
    }
    assert(!locks);return 0;
}
