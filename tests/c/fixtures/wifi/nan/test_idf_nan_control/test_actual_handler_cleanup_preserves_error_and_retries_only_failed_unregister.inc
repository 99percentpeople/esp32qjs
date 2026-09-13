
int main(void){
    assert(nan_clear_app_default_handlers()==-71&&s_app_default_handlers_set&&calls==1);
    error=0;assert(nan_clear_app_default_handlers()==ESP_OK&&!s_app_default_handlers_set&&calls==2);
    assert(nan_clear_app_default_handlers()==ESP_OK&&calls==2);
}
