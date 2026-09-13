
int main(void) {
 setup();
 struct eap_wsc_data *a=eap_wsc_init(&peers[0]),*b=eap_wsc_init(&peers[1]);
 assert(a && b && a!=b && a->wps==b->wps && a->wps==owner.wps);
 peers[0].eap_method_priv=a;
 eap_wsc_reset(&peers[0],a);
 assert(disable_calls==1); /* Native disable would free the calling EAP tree. */
 eap_wsc_reset(&peers[1],b);
 assert(disable_calls==2 && live==0);
 return 0;
}
