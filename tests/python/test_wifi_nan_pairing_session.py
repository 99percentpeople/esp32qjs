"""Deferred production Pairing/Service/Session ownership and scheduling cases.

Only Radio native commands and the existing worker/clock boundary are injected.
No independent pairing state machine is used as the subject under test.
"""
import unittest
from test_wifi_nan_session import NanSessionLifecycle


class NanPairingSession(unittest.TestCase):
    compile_case = NanSessionLifecycle.compile_case

    def test_confirmation_is_explicit_pin_copy_and_final_tx_precede_ready(self):
        self.compile_case(HELPER + r'''
int main(void){
 setup();esp32_mquickjs_wifi_nan_pairing_t*p=prepare(30000);tick();
 assert(!pair_starts&&!p->status.confirmed&&!p->pin);
 assert(!esp32_mquickjs_wifi_nan_pairing_confirm(p,true,1234));
 assert(esp32_mquickjs_wifi_nan_pairing_confirm(p,true,999999)==ESP_ERR_INVALID_STATE);
 tick();assert(pair_starts==1&&seen_pin==1234&&!p->pin&&!p->status.ready);
 pair_native.authenticated=pair_native.paired=true;pair_native.traffic_pending=true;
 pair_native.completed_us=clock_us;tick();assert(!p->status.ready);
 pair_native.traffic_pending=false;pair_native.completed_us=clock_us;tick();assert(p->status.ready);
 pair_close_error=ESP_ERR_TIMEOUT;esp32_mquickjs_wifi_nan_pairing_close(p,false);tick();
 assert(!p->status.retired&&s->pairing==p&&!s->status.closing);
 esp32_mquickjs_wifi_nan_pairing_t*other=NULL;uint8_t mac[6]={2,3,4,5,6,7};
 assert(esp32_mquickjs_wifi_nan_pairing_create(d,9,mac,true,1000,&other)==ESP_ERR_INVALID_STATE&&!other);
 pair_close_error=0;tick();assert(p->status.retired&&!s->pairing&&pair_starts==1);
 unsigned old_closes=pair_closes;esp32_mquickjs_wifi_nan_pairing_close(p,false);
 esp32_mquickjs_wifi_nan_pairing_release(p);assert(pair_closes==old_closes);finish();
}
''', security=True, pairing=True)

    def test_rejection_and_unanswered_deadline_never_start_authentication(self):
        self.compile_case(HELPER + r'''
int main(void){
 setup();esp32_mquickjs_wifi_nan_pairing_t*p=prepare(1000);
 assert(!esp32_mquickjs_wifi_nan_pairing_confirm(p,false,0));tick();
 assert(p->status.rejected&&p->status.retired&&!pair_starts&&!p->pin);
 esp32_mquickjs_wifi_nan_pairing_release(p);p=prepare(1000);
 clock_us+=1000000;tick();assert(p->status.timed_out&&p->status.retired&&!pair_starts);
 assert(esp32_mquickjs_wifi_nan_pairing_confirm(p,true,123456)==ESP_ERR_INVALID_STATE);
 esp32_mquickjs_wifi_nan_pairing_release(p);finish();
}
''', security=True, pairing=True)

    def test_native_failure_and_full_queue_keep_cleanup_and_service_owner(self):
        self.compile_case(HELPER + r'''
int main(void){
 setup();q.full=true;esp32_mquickjs_wifi_nan_pairing_t*p=prepare(30000);
 assert(!esp32_mquickjs_wifi_nan_pairing_confirm(p,true,123456));
 queue_full=true;assert(!esp32_mquickjs_wifi_nan_service()&&p->pin==123456&&!pair_starts);
 queue_full=false;pair_start_error=-73;pair_close_error=ESP_ERR_TIMEOUT;tick();
 assert(p->status.error==-73&&p->status.closing&&!p->pin);
 esp32_mquickjs_wifi_nan_discovery_close(d);tick();
 assert(!d->status.retired&&!service_cancels&&!p->status.retired);
 pair_close_error=0;tick();assert(p->status.retired&&d->status.retired&&service_cancels==1);
 esp32_mquickjs_wifi_nan_pairing_release(p);finish();
}
''', security=True, pairing=True)

    def test_parent_shutdown_does_not_wait_for_pairing_to_stop_before_stop(self):
        self.compile_case(HELPER + r'''
int main(void){
 setup();esp32_mquickjs_wifi_nan_pairing_t*p=prepare(30000);
 assert(!esp32_mquickjs_wifi_nan_pairing_confirm(p,true,123456));tick();
 pair_close_error=ESP_ERR_TIMEOUT;close_error=ESP_ERR_TIMEOUT;
 assert(!esp32_mquickjs_wifi_nan_prepare_runtime_destroy());run_queued(NULL);
 assert(s->status.closing&&!s->status.retired&&!p->status.retired&&closes==1);
 close_error=0;tick();assert(s->status.retired&&p->status.retired&&!s->pairing);
 esp32_mquickjs_wifi_nan_pairing_release(p);finish();
}
''', security=True, pairing=True)

    def test_completed_before_deadline_is_not_changed_by_delayed_worker(self):
        self.compile_case(HELPER + r'''
int main(void){
 setup();esp32_mquickjs_wifi_nan_pairing_t*p=prepare(1000);
 assert(!esp32_mquickjs_wifi_nan_pairing_confirm(p,true,123456));tick();
 pair_native.authenticated=pair_native.paired=true;pair_native.completed_us=p->deadline_us-1;
 clock_us=p->deadline_us+100000;tick();assert(p->status.ready&&!p->status.timed_out);
 esp32_mquickjs_wifi_nan_pairing_close(p,false);tick();esp32_mquickjs_wifi_nan_pairing_release(p);finish();
}
''', security=True, pairing=True)

    def test_detached_service_handles_and_cache_read_coalescing(self):
        self.compile_case(HELPER + r'''
int main(void){
 setup();uint32_t rev=99,other=99;bool ready=false;esp32_mquickjs_wifi_nan_credentials_t list;
 cached_credentials.count=1;cached_credentials.entries[0].identity=301;
 assert(!esp32_mquickjs_wifi_nan_discovery_credentials_request(d,&rev));
 assert(!esp32_mquickjs_wifi_nan_discovery_credentials_request(d,&other)&&rev==other);
 assert(!esp32_mquickjs_wifi_nan_discovery_credentials_read(d,rev,&ready,&list)&&!ready);tick();
 assert(credential_reads==1&&!esp32_mquickjs_wifi_nan_discovery_credentials_read(d,rev,&ready,&list)&&ready&&list.count==1);
 esp32_mquickjs_wifi_nan_pairing_t*p=prepare(1000);esp32_mquickjs_wifi_nan_session_close(s,false);tick();
 assert(p->status.retired&&!d->parent);esp32_mquickjs_wifi_nan_pairing_status_t status;
 esp32_mquickjs_wifi_nan_pairing_status(p,&status);assert(status.retired);
 esp32_mquickjs_wifi_nan_pairing_close(p,false);
 assert(esp32_mquickjs_wifi_nan_pairing_confirm(p,true,0)==ESP_ERR_INVALID_STATE);
 esp32_mquickjs_wifi_nan_pairing_t*next=NULL;uint8_t mac[6]={2,3,4,5,6,7};
 assert(esp32_mquickjs_wifi_nan_pairing_create(d,9,mac,true,1000,&next)==ESP_ERR_INVALID_STATE);
 assert(esp32_mquickjs_wifi_nan_discovery_credentials_request(d,&rev)==ESP_ERR_INVALID_STATE);
 esp32_mquickjs_wifi_nan_pairing_release(p);finish();
}
''', security=True, pairing=True)

    def test_verification_requires_confirmation_and_cache_commit_precedes_ready(self):
        self.compile_case(HELPER + r'''
int main(void){
 setup();uint8_t mac[6]={2,3,4,5,6,7};esp32_mquickjs_wifi_nan_pairing_t*p=NULL;
 assert(!esp32_mquickjs_wifi_nan_pairing_create(d,9,mac,true,30000,&p));
 assert(!esp32_mquickjs_wifi_nan_pairing_select(p,77));assert(!esp32_mquickjs_wifi_nan_pairing_activate(p));tick();
 assert(!pair_starts&&p->status.verification);
 assert(esp32_mquickjs_wifi_nan_pairing_confirm(p,true,123456)==ESP_ERR_INVALID_STATE);
 assert(!esp32_mquickjs_wifi_nan_pairing_confirm(p,true,0));tick();assert(pair_starts==1&&!seen_pin&&seen_credential==77);
 pair_native.paired=true;pair_native.completed_us=clock_us;pair_native.traffic_pending=true;tick();assert(!pair_commits);
 pair_native.traffic_pending=false;tick();assert(p->status.ready&&p->status.credential_id==301&&pair_commits==1);tick();assert(pair_commits==1);
 esp32_mquickjs_wifi_nan_pairing_close(p,false);tick();esp32_mquickjs_wifi_nan_pairing_release(p);finish();
}
''', security=True, pairing=True)

    def test_allocation_failure_activation_failure_and_identity_exhaustion(self):
        self.compile_case(HELPER + r'''
int main(void){
 setup();uint8_t mac[6]={2,3,4,5,6,7};esp32_mquickjs_wifi_nan_pairing_t*p=NULL;
 unsigned refs=d->references;fail_allocation=allocation_calls+1;
 assert(esp32_mquickjs_wifi_nan_pairing_create(d,9,mac,true,1000,&p)==ESP_ERR_NO_MEM);
 assert(!p&&!s_nan_pairing_handles&&d->references==refs);
 fail_allocation=0;s_nan_pairing_next_identity=UINT32_MAX;p=prepare(1000);
 assert(p->status.identity==UINT32_MAX&&!s_nan_pairing_next_identity);
 esp32_mquickjs_wifi_nan_pairing_close(p,false);tick();esp32_mquickjs_wifi_nan_pairing_release(p);p=NULL;
 assert(esp32_mquickjs_wifi_nan_pairing_create(d,9,mac,true,1000,&p)==ESP_ERR_NO_MEM&&!p);
 assert(!s_nan_pairing_handles&&d->references==refs);finish();
}
''', security=True, pairing=True)


PAIRING_BOUNDARY = r'''
static unsigned pair_starts,pair_closes,pair_commits,credential_reads;
static uint32_t seen_credential;
static esp_err_t pair_commit_error,credential_error;
static esp32_mquickjs_wifi_nan_credentials_t cached_credentials;
esp_err_t esp32_mquickjs_wifi_radio_nan_pairing_credentials(const esp32_mquickjs_wifi_radio_operation_t*t,
 uint8_t id,esp32_mquickjs_wifi_nan_credentials_t*out){assert(!lock_depth&&t->identity&&id);++credential_reads;*out=cached_credentials;return credential_error;}
esp_err_t esp32_mquickjs_wifi_radio_nan_pairing_commit(const esp32_mquickjs_wifi_radio_operation_t*t,
 uint32_t id,uint32_t*out){assert(!lock_depth&&t->identity&&id);++pair_commits;*out=pair_commit_error?0:301;return pair_commit_error;}
static unsigned bootstrap_sends;
static bool bootstrap_response,bootstrap_accept;
esp_err_t esp32_mquickjs_wifi_radio_nan_bootstrap_send(const esp32_mquickjs_wifi_radio_operation_t*op,
 wifi_nan_followup_params_t*params,uint32_t id,uint32_t*context,bool response,bool accept){
 assert(!lock_depth&&native_started&&op->identity&&params->inst_id==7&&params->peer_inst_id==9&&id&&context&&!*context);
 ++bootstrap_sends;bootstrap_response=response;bootstrap_accept=accept;*context=4321;
 message_tx=(esp32_mquickjs_wifi_nan_message_tx_status_t){.identity=id,.ticket=bootstrap_sends,.entered=true,.allocated=true};return 0;
}
static uint32_t seen_pin;
static esp_err_t pair_start_error,pair_close_error;
static esp32_mquickjs_wifi_nan_pasn_status_t pair_native;
esp_err_t esp32_mquickjs_wifi_radio_nan_pairing_start(const esp32_mquickjs_wifi_radio_operation_t*t,
 const esp32_mquickjs_wifi_nan_pairing_config_t*c,esp32_mquickjs_wifi_nan_pasn_status_t*out){
 assert(!lock_depth&&t->identity&&c->service_identity&&c->service_id&&c->peer_service_id==9);
 ++pair_starts;seen_pin=c->pincode;seen_credential=c->credential_id;
 pair_native=(esp32_mquickjs_wifi_nan_pasn_status_t){.identity=100+pair_starts,.active=true};
 memcpy(pair_native.peer,c->peer,6);*out=pair_native;return pair_start_error;
}
esp_err_t esp32_mquickjs_wifi_radio_nan_pairing_poll(const esp32_mquickjs_wifi_radio_operation_t*t,
 uint32_t id,bool close,esp32_mquickjs_wifi_nan_pasn_status_t*out){
 assert(!lock_depth&&t->identity&&id==pair_native.identity);
 if(close){++pair_closes;pair_native.closing=true;if(!pair_close_error){pair_native.active=false;pair_native.retired=true;}}
 *out=pair_native;return close?pair_close_error:ESP_OK;
}
'''

HELPER = r'''
static esp32_mquickjs_wifi_nan_session_t*s;
static esp32_mquickjs_wifi_nan_discovery_t*d;
static struct esp32_mquickjs_event_queue q={.references=1};
static void tick(void){clock_us+=100000;assert(esp32_mquickjs_wifi_nan_service());run_queued(NULL);}
static void setup(void){
 assert(!esp32_mquickjs_wifi_nan_open_runtime());wifi_nan_sync_config_t config={.op_channel=6};
 assert(!esp32_mquickjs_wifi_nan_session_create(&config,1000,&s));
 assert(!esp32_mquickjs_wifi_nan_session_activate(s));tick();
 wifi_nan_pairing_cfg_t pairing={.pairing_setup=true,.npk_nik_caching=true,.pairing_verification=true,.bootstrapping_methods=WIFI_NAN_BOOTSTRAP_PIN_CODE_KEYPAD};
 esp32_mquickjs_wifi_nan_service_config_t cfg={.subscribe={.service_name="pin-pairing",.pairing=&pairing}};
 assert(!esp32_mquickjs_wifi_nan_discovery_create(s,false,&cfg,1000,&d));
 pairing.pairing_setup=false;assert(d->config.subscribe.pairing->pairing_setup);
 assert(d->config.subscribe.security_reqd&&d->config.subscribe.security_cfg->num_credentials==1);
 assert(d->config.subscribe.security_cfg->creds[0].csid==WIFI_NAN_CSID_NCS_PK_PASN_128);
 assert(d->status.security_required&&!d->status.credential_count);
 assert(!esp32_mquickjs_wifi_nan_discovery_activate(d,&q));tick();assert(d->status.ready);
}
static __attribute__((unused)) esp32_mquickjs_wifi_nan_pairing_t*prepare(uint32_t timeout){
 uint8_t mac[6]={2,3,4,5,6,7};esp32_mquickjs_wifi_nan_pairing_t*p=NULL;
 assert(!esp32_mquickjs_wifi_nan_pairing_create(d,9,mac,true,timeout,&p));
 assert(!esp32_mquickjs_wifi_nan_pairing_activate(p));return p;
}
static void finish(void){
 if(!s->status.retired){esp32_mquickjs_wifi_nan_session_close(s,false);tick();}
 assert(q.closed);esp32_mquickjs_wifi_nan_discovery_queue_closed(d);
 esp32_mquickjs_wifi_nan_discovery_release(d);esp32_mquickjs_wifi_nan_session_release(s);
 assert(!s_nan_pairing_handles&&!s_nan_discovery_handles&&!s_nan_handles&&!allocations&&q.references==1);
}
'''
