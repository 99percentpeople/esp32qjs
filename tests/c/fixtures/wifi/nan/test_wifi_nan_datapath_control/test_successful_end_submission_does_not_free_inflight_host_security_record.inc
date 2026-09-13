
int main(void){
 NAN_DATA_LOCK();nan_ndp_confirm_teardown(peer,7);NAN_DATA_UNLOCK();
 assert(!depth&&end_calls==1&&!resets&&live_ndl&&(event_bits&NDP_REJECTED));
}
