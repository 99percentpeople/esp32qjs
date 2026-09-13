
int main(void){
 failure_mode=FAILURE_MODE;
 wifi_nan_discovery_security_params_t cfg={.num_credentials=2,.creds={{.csid=1,.use_pmk=true},{.csid=1,.use_pmk=true}}};
 memset(cfg.creds[0].pmk,0x67,32);memset(cfg.creds[1].pmk,0x78,32);
 wifi_nan_security_params_t derived[4];memset(derived,0xa5,sizeof(derived));
 assert(nan_derive_security_params("test-only",&cfg,derived)==ESP_FAIL);
 for(size_t i=0;i<sizeof(derived);i++)assert(!((uint8_t*)derived)[i]);
 assert(!own.derived_security[0].type);
}
