
int main(void){
 struct hostapd_data h={0};uint32_t identity=0;
 assert(esp32qjs_wps_ap_result_reserve(&identity)==0);
 assert(esp32qjs_wps_ap_result_bind(&h)==0);
 esp32qjs_wps_ap_init_failed(&h,-74);
 assert(!posts && !releases && live==1 && s_wps_ap_result->status.sdk_attached);
 assert(s_wps_ap_result->status.close_prepared && s_wps_ap_result->status.error==-74);
 esp32qjs_wps_ap_result_cleanup_error(&h,-95);
 assert(s_wps_ap_result->status.error==-74 && s_wps_ap_result->status.cleanup_error==-95);
 int foreign=0;esp32qjs_wps_ap_result_cleanup_error(&foreign,-96);
 assert(s_wps_ap_result->status.cleanup_error==-95);
 /* Fixture teardown only; managed native release remains a separate task. */
 esp32qjs_wps_ap_result_free();return 0;
}
