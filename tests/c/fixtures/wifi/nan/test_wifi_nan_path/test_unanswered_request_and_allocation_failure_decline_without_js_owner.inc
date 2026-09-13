
int main(void){
 setup(true);incoming();fail_allocation=allocation_calls+1;tick();
 assert(path_responses==1&&!path_response_accept&&!s_nan_path_handles&&!s->status.closing);
 path_native[0].native_deleted=path_native[0].frames_retired=true;tick();
 assert(!path_native[0].identity);fail_allocation=0;incoming();tick();
 assert(s_nan_path_handles==1);
 clock_us+=10000000;tick();assert(path_responses==2&&!path_response_accept);
 path_native[0].native_deleted=path_native[0].frames_retired=true;tick();assert(!s_nan_path_handles);
 finish();
}
