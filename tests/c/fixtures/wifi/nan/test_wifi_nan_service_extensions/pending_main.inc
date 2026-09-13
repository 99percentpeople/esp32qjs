
int main(void){
 int mode=MATCH_MODE;struct ndl_info ndl={0};uint8_t peer[6]={2,3,4,5,6,7};
 own.user_cfg.num_credentials=1;own.derived_security[0].csid_bitmap=2;
 memset(own.derived_security[0].nd_pmk,0x67,32);match_index=mode==2?0:-1;
 s_pending_scia.pub_id=7;s_pending_scia.has_csid=true;s_pending_scia.has_pmkid=mode!=1;s_pending_scia.csid_bitmap=2;
 s_pending_m1.valid=true;s_pending_m1.pub_id=7;
 nan_security_apply_pending(&ndl,&own,7,peer,peer);
 if(mode==2)assert(ndl.security_ctx.type==WIFI_NAN_SECURITY_ENCRYPTED&&ndl.security_ctx.nd_pmk[0]==0x67&&ndl.handshake_state==1);
 else assert(!ndl.security_ctx.type&&!ndl.handshake_state);
}
