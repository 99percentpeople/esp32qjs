
int main(void){
    for(unsigned nth=1;nth<=4;++nth){allocations=mu_calls=0;fail_at=nth;
        assert(__wrap_hal_enable_rx_statistics(true,true)==ESP_ERR_NO_MEM && !live);
        assert(mu_calls==(nth==4?1U:0U));}
    fail_at=0;assert(__wrap_hal_enable_rx_statistics(true,true)==ESP_OK && live==4);
    assert(__wrap_hal_enable_rx_statistics(true,true)==ESP_OK && live==4);
    assert(__wrap_hal_enable_rx_statistics(false,true)==ESP_OK && live==1);
    assert(__wrap_hal_enable_rx_statistics(false,false)==ESP_OK && !live);
    for(unsigned aci=0;aci<4;++aci){
        fail_at=0;assert(__wrap_hal_enable_tx_statistics((aci+1)%4,true)==ESP_OK && live==3);
        for(unsigned nth=1;nth<=3;++nth){allocations=0;fail_at=nth;
            assert(__wrap_hal_enable_tx_statistics(aci,true)==ESP_ERR_NO_MEM && live==3);
            assert(!(aci_bitmap&(1U<<aci)) && !esp_test_tx_statistics[aci] && !esp_test_tx_tb_statistics[aci]);
            for(unsigned j=0;j<6;++j)assert(!esp_test_tx_fail_statistics[aci][j]);}
        fail_at=0;assert(__wrap_hal_enable_tx_statistics(aci,true)==ESP_OK && live==6);
        unsigned before=allocations;assert(__wrap_hal_enable_tx_statistics(aci,true)==ESP_OK && allocations==before);
        assert(__wrap_hal_enable_tx_statistics(aci,false)==ESP_OK && live==3);
        assert(__wrap_hal_enable_tx_statistics((aci+1)%4,false)==ESP_OK && !live);
    }
    assert(__wrap_hal_enable_tx_statistics(4,true)==ESP_ERR_INVALID_ARG && !live);
    return 0;
}
