
int main(void){
 assert(!esp32_mquickjs_wifi_nan_sdk_security_install_group_keys()&&!installs);
 s_nan_ctx.group_mgmt_prot=true;
 mac_error=-71;assert(esp32_mquickjs_wifi_nan_sdk_security_install_group_keys()==-71&&!ensures&&!installs);
 mac_error=0;ensure_error=-72;assert(esp32_mquickjs_wifi_nan_sdk_security_install_group_keys()==-72&&!installs);
 ensure_error=0;install_error_at=1;
 assert(esp32_mquickjs_wifi_nan_sdk_security_install_group_keys()==-73&&installs==1);
 installs=0;install_error_at=2;
 assert(esp32_mquickjs_wifi_nan_sdk_security_install_group_keys()==-73&&installs==2);
 installs=0;install_error_at=0;
 assert(!esp32_mquickjs_wifi_nan_sdk_security_install_group_keys()&&installs==2);
}
