
int main(void){
    fail_at=1;esp_nan_app_init();assert(!nan_event_group&&!s_nan_data_lock&&!live&&!registered&&calls==1);
    calls=0;fail_at=2;esp_nan_app_init();assert(!nan_event_group&&!s_nan_data_lock&&!live&&!registered&&calls==2);
    fail_at=0;esp_nan_app_init();assert(live==2&&registered);unsigned before=calls;
    void *group=nan_event_group,*lock=s_nan_data_lock;esp_nan_app_init();
    assert(calls==before&&group==nan_event_group&&lock==s_nan_data_lock&&live==2);
    esp_nan_app_deinit();assert(!live&&!registered&&!nan_event_group&&!s_nan_data_lock);
}
