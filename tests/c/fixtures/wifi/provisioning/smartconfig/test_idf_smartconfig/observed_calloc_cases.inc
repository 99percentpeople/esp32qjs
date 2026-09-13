
int main(void) {
    next_result=&allocation;
    assert(esp32qjs_smartconfig_calloc(92,1)==&allocation);
    assert(calls==1 && failures==0 && seen_count==92 && seen_size==1);
    next_result=NULL;
    assert(esp32qjs_smartconfig_calloc(2824,1)==NULL);
    assert(calls==2 && failures==1 && seen_count==2824 && seen_size==1);
    assert(esp32qjs_smartconfig_calloc(SIZE_MAX,2)==NULL);
    assert(calls==3 && failures==2 && seen_count==SIZE_MAX && seen_size==2);
    assert(esp32qjs_smartconfig_calloc(0,1)==NULL);
    assert(esp32qjs_smartconfig_calloc(1,0)==NULL);
    assert(calls==5 && failures==2);
    return 0;
}
