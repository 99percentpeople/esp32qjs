
int main(void){
 esp32_mquickjs_wifi_configuration_execution_t result;bool enabled=false;
 setup_restart();stopped_source=true;running=false;s_wifi_application.acquired=false;s_wifi_state.radio_lease.acquired=false;
 assert(!wifi_restart_interfaces_inner(WIFI_MODE_NULL,true,&result,&enabled,false));
 assert(phase_calls[10]==2&&!selected_wapi&&reconstructed&&replayed&&running); /* Source Station prepare plus policy select. */
 setup_restart();stopped_source=true;running=false;s_wifi_application.acquired=false;s_wifi_state.radio_lease.acquired=false;
 phase_failure=1;selected_wapi=true;
 assert(wifi_restart_interfaces_inner(WIFI_MODE_NULL,true,&result,&enabled,false)==-101);
 assert(phase_calls[10]==1&&selected_wapi&&!reconstructed); /* Source prepare ran; policy select did not. */
}
