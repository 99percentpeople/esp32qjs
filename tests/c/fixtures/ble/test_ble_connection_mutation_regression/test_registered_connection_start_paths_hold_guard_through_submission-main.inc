
int main(void) {
    init();contended=true;driver_result=0;
    pthread_t thread;assert(!pthread_create(&thread,NULL,host,NULL));
    assert(START(NULL,NULL,1,NULL));
    assert(!pthread_join(thread,NULL));
    assert(blocked_attempt && calls==1);
    contended=false;driver_result=2;
    assert(!START(NULL,NULL,1,NULL));
    assert(!pthread_mutex_destroy(&mutex));
}
