
int main(void){
 int frequencies[]={2437,5180,0};struct nan_de de={0};
 struct nan_de_service service={.type=NAN_DE_SUBSCRIBE,.freq_list=frequencies};
 struct os_reltime now={.sec=1};esp32qjs_nan_usd_subscribe_channel(&de,&service,&now);assert(service.freq==2437);
 now.sec=2;de.listen_freq=2437;esp32qjs_nan_usd_subscribe_channel(&de,&service,&now);assert(service.freq==2437);
 de.listen_freq=0;de.tx_wait_end_freq=2437;esp32qjs_nan_usd_subscribe_channel(&de,&service,&now);assert(service.freq==2437);
 de.tx_wait_end_freq=0;esp32qjs_nan_usd_subscribe_channel(&de,&service,&now);assert(service.freq==5180);
 now.sec=3;esp32qjs_nan_usd_subscribe_channel(&de,&service,&now);assert(service.freq==2437);
}
