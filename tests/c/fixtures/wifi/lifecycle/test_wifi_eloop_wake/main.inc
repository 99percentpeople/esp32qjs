
int main(void){
 eloop_run_timer(NULL);assert(posts==1&&!arms&&!locked);
 ipc_error=257;eloop_run_timer(NULL);assert(posts==2&&arms==1&&disarms==1&&!locked);
 /* No removal or new allocation: repeated pressure keeps the same timer. */
 eloop_run_timer(NULL);assert(arms==2&&disarms==2);
 cancel_during_post=true;eloop_run_timer(NULL);assert(arms==2&&!locked);cancel_during_post=false;empty=false;
 destroy_during_post=true;eloop_run_timer(NULL);assert(arms==2&&!locked);destroy_during_post=false;
 running=true;eloop_data_lock=NULL;eloop_run_timer(NULL);assert(arms==2&&!locked);
 eloop_data_lock=(void*)1;ipc_error=0;eloop_run_timer(NULL);assert(arms==2&&!locked);
 return 0;
}
