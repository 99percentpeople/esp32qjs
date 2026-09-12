"""Deferred execution of the patched production SDK one-shot wake function."""
import os
from pathlib import Path
import sys
import unittest
from test_wireless_control_regression import compile_run, function

ROOT = Path(__file__).resolve().parents[2]


class ElooopWake(unittest.TestCase):
    def test_failed_wake_rearms_retained_work_and_respects_teardown(self):
        sdk = os.environ.get('IDF_PATH')
        if not sdk:
            self.skipTest('Set IDF_PATH to the reviewed SDK')
        sys.path.insert(0, str(ROOT / 'scripts'))
        from patch_idf_eloop import patch_source
        source = patch_source((Path(sdk) / 'components/wpa_supplicant/port/eloop.c').read_bytes()).decode()
        compile_run(self, BOUNDARIES + function(source, 'eloop_run_timer') + MAIN)


BOUNDARIES = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
typedef struct {int(*fn)(void*);void*arg;unsigned arg_size;}wifi_ipc_config_t;
static struct {int timeout,eloop_timer;}eloop;
static void *eloop_data_lock=(void*)1;
static bool running=true,empty,locked,cancel_during_post,destroy_during_post;
static unsigned posts,arms,disarms;
static int ipc_error;
#define ELOOP_LOCK() do{assert(!locked);locked=true;}while(0)
#define ELOOP_UNLOCK() do{assert(locked);locked=false;}while(0)
static bool eloop_is_running(void){assert(locked);return running;}
static bool dl_list_empty(void*p){assert(p==&eloop.timeout&&locked);return empty;}
static void os_timer_disarm(void*p){assert(p==&eloop.eloop_timer&&locked);disarms++;}
static void os_timer_arm(void*p,unsigned ms,int repeat){assert(p==&eloop.eloop_timer&&locked&&ms==10&&!repeat);arms++;}
static int eloop_run_wrapper(void*p){assert(!p);return 0;}
static int esp_wifi_ipc_internal(wifi_ipc_config_t*config,bool sync){
 assert(!locked&&!sync&&config->fn==eloop_run_wrapper&&!config->arg&&!config->arg_size);posts++;
 if(cancel_during_post)empty=true;
 if(destroy_during_post)running=false;
 return ipc_error;
}
'''

MAIN = r'''
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
'''
