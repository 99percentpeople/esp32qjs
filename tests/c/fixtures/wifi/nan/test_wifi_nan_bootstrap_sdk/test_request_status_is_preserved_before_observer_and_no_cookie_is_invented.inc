
int main(void){
 uint8_t peer[6]={2,3,4,5,6,7};struct nan_cb_npba_t npba={.type=1,.status=2,.methods=32};
 assert(nan_app_parse_npba_from_receive(7,9,peer,&npba));assert(notices==1&&last_status==2);
 npba.status=0;assert(nan_app_parse_npba_from_receive(7,9,peer,&npba)&&last_status==0);
 npba.type=2;npba.status=1;npba.methods=2;
 assert(nan_app_parse_npba_from_receive(7,9,peer,&npba)&&last_status==1);
 assert(!nan_app_parse_npba_from_receive(7,9,NULL,&npba));
 unsigned count=notices;nan_app_bootstrap_indication(9,7,NULL,32);nan_app_bootstrap_completed(0,9,7,NULL,2,0);
 assert(notices==count);return 0;
}
