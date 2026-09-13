
int main(void){
 setup();incoming();int64_t deadline=s->pairing_request.deadline_us;tick();
 esp32_mquickjs_wifi_nan_pairing_t*p=NULL;
 assert(!esp32_mquickjs_wifi_nan_pairing_receive(d,&p)&&p);
 assert(esp32_mquickjs_wifi_nan_pairing_deliver(p));
 clock_us=deadline;tick();assert(p->status.timed_out&&!pair_starts&&!p->status.retired);
 tick();assert(bootstrap_sends==1&&bootstrap_response&&!bootstrap_accept);
 assert(!p->status.retired);recycle(true);assert(p->status.retired&&!pair_starts);
 esp32_mquickjs_wifi_nan_pairing_release(p);finish();
}
