
#define api(n) esp32_mquickjs_wifi_tx_rate_##n
static esp32_mquickjs_wifi_tx_rate_state_t state;
static esp32_mquickjs_wifi_tx_rate_lease_t lease;
static wifi_tx_rate_config_t native[2];
static wifi_tx_rate_config_t requested={.phymode=WIFI_PHY_MODE_11G,.rate=WIFI_PHY_RATE_24M};
static unsigned writes,selected;
static int first,second;
static esp_err_t writer(void *opaque,wifi_interface_t interface,const wifi_tx_rate_config_t *config) {
    assert(opaque==&state && lease.identity==73 && lease.generation==7);
    assert((unsigned)(interface==WIFI_IF_STA?0:1)==selected);
    native[selected]=*config; ++writes;
    int error=first;first=second;second=0;return error;
}
static void setup(unsigned index) {
    state=(esp32_mquickjs_wifi_tx_rate_state_t){.next_identity=10};
    lease=(esp32_mquickjs_wifi_tx_rate_lease_t){0};
    for(unsigned i=0;i<2;++i) {
        native[i]=(wifi_tx_rate_config_t){.phymode=WIFI_PHY_MODE_11G,.rate=i?WIFI_PHY_RATE_9M:WIFI_PHY_RATE_6M};
        state.records[i]=(esp32_mquickjs_wifi_tx_rate_record_t){.generation=7,.write_identity=i+1,.known=true,.config=native[i]};
    }
    selected=index;writes=0;first=second=0;
}
static int borrow(wifi_interface_t interface) {return api(borrow)(&state,7,73,interface,&requested,&lease,writer,&state);}
static int restore(uint32_t generation,uint32_t owner) {return api(restore)(&state,generation,owner,&lease,writer,&state);}
int main(void) {
    for(unsigned index=0;index<2;++index) {
        wifi_interface_t interface=index?WIFI_IF_AP:WIFI_IF_STA;
        setup(index);
        if(index && !CONFIG_ESP_WIFI_SOFTAP_SUPPORT) {
            assert(borrow(interface)==ESP_ERR_NOT_SUPPORTED && !writes && !lease.identity);continue;
        }
        wifi_tx_rate_config_t previous=native[index],other=native[1-index];
        esp32_mquickjs_wifi_tx_rate_state_t before=state;
        assert(!api(borrow_admission)(&state,7,interface,&requested,&lease));
        assert(!writes && !memcmp(&before,&state,sizeof(state)));
        assert(!borrow(interface) && lease.identity==73 && lease.interface==interface && writes==1);
        assert(lease.write_identity==10 && !memcmp(&lease.previous,&previous,sizeof(previous)));
        assert(!memcmp(&native[1-index],&other,sizeof(other)));
        esp32_mquickjs_wifi_tx_rate_lease_t frozen=lease;
        assert(restore(8,73)==ESP_ERR_INVALID_STATE && restore(7,74)==ESP_ERR_INVALID_STATE && writes==1);
        assert(!memcmp(&frozen,&lease,sizeof(lease)));
        assert(borrow(interface)==ESP_ERR_INVALID_STATE && writes==1);
        first=41;second=0;
        assert(restore(7,73)==41 && writes==3 && lease.restore_pending && lease.restore_error==41);
        assert(lease.write_identity==11 && native[index].rate==requested.rate);
        assert(!memcmp(&lease.previous,&previous,sizeof(previous)));
        assert(!restore(7,73) && writes==4 && !lease.identity && !lease.restore_pending);
        assert(!memcmp(&native[index],&previous,sizeof(previous)));
        assert(restore(7,73)==ESP_ERR_INVALID_STATE && writes==4);
        setup(index);first=42;second=0;
        assert(borrow(interface)==42 && writes==2 && !lease.identity && state.records[index].known);
        setup(index);first=42;second=43;
        assert(borrow(interface)==42 && writes==2 && lease.identity==73 && lease.restore_pending && lease.restore_error==43);
        assert(state.records[index].uncertain && !state.records[index].known);
        assert(!restore(7,73) && !lease.identity && state.records[index].known);
        for(unsigned bad=0;bad<8;++bad) {
            setup(index);
            switch(bad) {
            case 0:state.records[index].known=false;break;
            case 1:state.records[index].uncertain=true;break;
            case 2:state.records[index].generation++;break;
            case 3:state.records[index].write_identity=0;break;
            case 4:state.records[index].config.rate=(wifi_phy_rate_t)9999;break;
            case 5:state.next_identity=0;break;
            case 6:state.next_identity=UINT32_MAX;break;
            case 7:lease.restore_pending=true;break;
            }
            before=state;frozen=lease;
            assert(borrow(interface)==ESP_ERR_INVALID_STATE && !writes);
            assert(!memcmp(&before,&state,sizeof(state)) && !memcmp(&frozen,&lease,sizeof(lease)));
        }
        setup(index);state.next_identity=UINT32_MAX-1;
        assert(!borrow(interface) && !restore(7,73) && state.next_identity==0 && !lease.identity);
        setup(index);assert(!borrow(interface));state.records[index].write_identity++;
        assert(restore(7,73)==ESP_ERR_INVALID_STATE && writes==1 && lease.identity==73);
        setup(index);assert(!borrow(interface));lease.interface=(wifi_interface_t)99;
        assert(restore(7,73)==ESP_ERR_INVALID_STATE && writes==1 && lease.identity==73);
        setup(index);assert(!borrow(interface));state.next_identity=0;
        assert(restore(7,73)==ESP_ERR_INVALID_STATE && writes==1 && lease.restore_pending);
    }
    return 0;
}
