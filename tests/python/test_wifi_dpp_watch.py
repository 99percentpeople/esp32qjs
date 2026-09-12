"""Deferred production observation polling, Future gating and lifetime checks.

Full Session source and real watch helpers; SDK/worker and EventQueue lifetime
operations are injected boundaries. Not a full Future-core/reaper scheduling or
VM constructor proof. Execute only in the concentrated Wi-Fi validation phase.
"""
import unittest
from test_wifi_dpp_session import ROOT as COMPONENT, TYPES, BOUNDARIES, CASES, headers, unit
from test_wireless_control_regression import compile_run
from wireless_vm_fixture import extract


class DppWatch(unittest.TestCase):
    def test_terminal_ordering_saturation_close_and_retained_budget(self):
        public = (COMPONENT / 'src/modules/wifi_dpp/esp32_mquickjs_wifi_dpp.c').read_text()
        boundaries = BOUNDARIES.replace('assert(!locks);locks++;', 'assert(locks<2);locks++;')
        boundaries = boundaries.replace('assert(locks==1);locks--;', 'assert(locks);locks--;')
        boundaries = boundaries.replace('for(size_t i=0;i<allocation_size;i++)assert(!((uint8_t*)p)[i]);', '')
        code = TYPES + headers() + boundaries
        code += unit(COMPONENT / 'src/modules/wifi_dpp/esp32_mquickjs_wifi_dpp_session.c')
        code += CASES[:CASES.index('int main(void)')]
        code += 'typedef struct mock_queue esp32_mquickjs_event_queue_t;\n'
        code += public[public.index('#define DPP_WATCH_HANDLES'):public.index('/* Compare semantic fields')]
        code += QUEUES
        for name in ('dpp_watch_same', 'dpp_watch_closed', 'dpp_watch_destroyed',
                     'esp32_mquickjs_wifi_dpp_poll_observations'):
            code += extract(public, name)
        compile_run(self, code + MAIN)


QUEUES = r'''
struct mock_queue {
 dpp_watch_source_t *source;dpp_watch_event_t last;
 unsigned refs,sends,drops;bool closed,disposed,full;
};
static struct mock_queue q;
static void dpp_watch_closed(void *);
static void dpp_watch_destroyed(void *);
static bool esp32_mquickjs_event_queue_retain(esp32_mquickjs_event_queue_t *queue){
 assert(locks==1);if(queue->disposed)return false;queue->refs++;return true;
}
static void esp32_mquickjs_event_queue_release(esp32_mquickjs_event_queue_t *queue){assert(!locks && queue->refs);queue->refs--;}
static bool esp32_mquickjs_event_queue_close(esp32_mquickjs_event_queue_t *queue){
 assert(!locks);if(queue->closed)return false;queue->closed=true;dpp_watch_closed(queue->source);return true;
}
static void esp32_mquickjs_event_queue_request_close(esp32_mquickjs_event_queue_t *queue){(void)esp32_mquickjs_event_queue_close(queue);}
static bool esp32_mquickjs_event_queue_try_send_from_callback(esp32_mquickjs_event_queue_t *queue,const void *event){
 assert(!locks && queue->refs);queue->sends++;
 if(queue->closed || queue->full){queue->drops++;return false;}
 queue->last=*(const dpp_watch_event_t *)event;return true;
}
'''

MAIN = r'''
static void observe(esp32_mquickjs_wifi_dpp_session_t *s){
 assert(!s_dpp_watch_handles && !s_dpp_watch_sources[0]);memset(&q,0,sizeof(q));
 dpp_watch_source_t *source=heap_caps_calloc(1,sizeof(*source),1);assert(source);
 assert(esp32_mquickjs_wifi_dpp_session_retain(s));
 source->session=s;source->queue=&q;source->context_bound=true;q.source=source;
 s_dpp_watch_sources[0]=source;s_dpp_watch_handles++;
}
static void destroy_queue(void){
 assert(q.closed && !q.refs);q.disposed=true;dpp_watch_destroyed(q.source);q.source=NULL;
 assert(!s_dpp_watch_handles && !s_dpp_watch_sources[0]);
}
int main(void){
 reset();esp32_mquickjs_wifi_dpp_session_t *s=create();tick();uri_generated=true;radio.worker.native.uri_available=true;tick();observe(s);
 assert(esp32_mquickjs_wifi_dpp_poll_observations(false) && q.sends==1 && q.last.sequence==1);
 assert(!esp32_mquickjs_wifi_dpp_poll_observations(false) && q.sends==1);
 s->status.worker_busy=true;assert(!esp32_mquickjs_wifi_dpp_poll_observations(false));s->status.worker_busy=false;
 assert(esp32_mquickjs_wifi_dpp_session_wait_begin(s,false));
 assert(!esp32_mquickjs_wifi_dpp_poll_observations(false));
 assert(!esp32_mquickjs_wifi_dpp_session_uri(s,NULL,0,true));
 esp32_mquickjs_wifi_dpp_session_wait_end(s,false);
 q.full=true;assert(esp32_mquickjs_wifi_dpp_poll_observations(false) && q.drops==1);
 assert(!esp32_mquickjs_wifi_dpp_poll_observations(false));
 terminal=true;tick();tick();assert(s->status.configs_ready);
 assert(esp32_mquickjs_wifi_dpp_session_wait_begin(s,false));
 assert(!esp32_mquickjs_wifi_dpp_poll_observations(false));
 assert(!esp32_mquickjs_wifi_dpp_session_configs_commit(s));
 esp32_mquickjs_wifi_dpp_session_wait_end(s,false);q.full=false;
 assert(esp32_mquickjs_wifi_dpp_poll_observations(false) && q.last.sequence==3 && q.last.status.configs_consumed);
 assert(esp32_mquickjs_wifi_dpp_session_wait_begin(s,true));
 esp32_mquickjs_wifi_dpp_session_close(s,false);
 for(unsigned i=0;i<8 && !s->status.retired;i++)tick();assert(s->status.retired);
 assert(!esp32_mquickjs_wifi_dpp_poll_observations(false));
 esp32_mquickjs_wifi_dpp_session_wait_end(s,true);
 assert(esp32_mquickjs_wifi_dpp_poll_observations(false) && q.last.status.retired);
 assert(esp32_mquickjs_wifi_dpp_poll_observations(true) && q.closed && s_dpp_watch_handles==1);
 destroy_queue();close_all(s);

 reset();s=create();observe(s);esp32_mquickjs_wifi_dpp_session_release(s);
 assert(s_dpp_handles==1);assert(esp32_mquickjs_wifi_dpp_poll_observations(true));
 for(unsigned i=0;i<8&&s_dpp_active;i++)tick();assert(!s_dpp_handles && s_dpp_watch_handles==1);destroy_queue();

 reset();s=create();observe(s);q.disposed=true;
 (void)esp32_mquickjs_wifi_dpp_poll_observations(true);
 assert(!s_dpp_watch_sources[0] && !q.source->session && s_dpp_watch_handles==1);
 assert(esp32_mquickjs_event_queue_close(&q));destroy_queue();close_all(s);

 reset();s=create();observe(s);q.source->last.sequence=UINT32_MAX;s->status.error=7;
 (void)esp32_mquickjs_wifi_dpp_poll_observations(false);
 assert(q.closed && !q.sends && !s_dpp_watch_sources[0]);destroy_queue();close_all(s);
 assert(allocations==frees && !s_dpp_handles && !s_dpp_workers);return 0;
}
'''
