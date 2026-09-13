
int main(void){
 CONFIG_TYPE config={.service_name="test"};uint8_t id=0;
 assert(CHECKED_CALL(&config,&id)==-72&&!id&&!claimed&&!heap_live&&submits==1&&!depth);
 allocations=0;fail_at=1;assert(CHECKED_CALL(&config,&id)==ESP_ERR_NO_MEM&&!id&&!heap_live&&submits==1);
 allocations=0;fail_at=2;pairing_t pairing={0};config.pairing=&pairing;
 assert(CHECKED_CALL(&config,&id)==ESP_ERR_NO_MEM&&!id&&!heap_live&&submits==1);
 config.pairing=NULL;fail_at=0;native_error=0;
 assert(!CHECKED_CALL(&config,&id)&&id==7&&claimed&&!heap_live&&submits==2&&!depth);
 assert(CHECKED_CALL(&config,&id)==ESP_ERR_INVALID_ARG&&submits==2);
 id=0;memset(config.service_name,'x',sizeof(config.service_name));
 assert(CHECKED_CALL(&config,&id)==ESP_ERR_INVALID_ARG&&submits==2);
}
