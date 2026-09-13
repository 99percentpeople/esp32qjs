
int main(void){
 setup();unsigned refs=d->references;incoming();queue_full=true;
 assert(!esp32_mquickjs_wifi_nan_service());clock_us=s->pairing_request.deadline_us;
 queue_full=false;tick();assert(!s->pairing&&!s->pairing_request.service&&d->references==refs);
 assert(!d->status.pairing_request_pending&&!pair_starts&&!bootstrap_sends);
 incoming();fail_allocation=allocation_calls+1;tick();fail_allocation=0;
 assert(!s->pairing&&!s->pairing_request.service&&d->references==refs);
 assert(d->status.pairing_requests_dropped==2&&!s_nan_pairing_handles);finish();
}
