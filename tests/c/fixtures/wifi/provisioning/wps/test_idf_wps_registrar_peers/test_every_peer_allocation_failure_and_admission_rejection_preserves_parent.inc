
int main(void) {
 for (int nth=1;nth<=3;nth++) {
  setup();fail_at=nth;
  assert(!eap_wsc_init(&peers[0]) && !live && !disable_calls);
  assert(hapd.wps==&context && context.registrar && template.use_cred==&credential);
 }
 for (int fault=0;fault<10;fault++) {
  setup();
  if(fault==0)peers[0].cfg=NULL;
  if(fault==1)peers[0].identity=NULL;
  if(fault==2)peers[0].identity=(u8*)WSC_ID_REGISTRAR;
  if(fault==3)owner_kind=WPS_OWNER_ENROLLEE;
  if(fault==4)owner_status=0;
  if(fault==5)owner.wps=NULL;
  if(fault==6)cfg.wps=NULL;
  if(fault==7)context.registrar=NULL;
  if(fault==8)template.use_cred=NULL;
  if(fault==9)hapd.wps=NULL;
  assert(!eap_wsc_init(&peers[0]) && !allocations && !live && !disable_calls);
 }
 setup();assert(!eap_wsc_init(NULL));
 return 0;
}
