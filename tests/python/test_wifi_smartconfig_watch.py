"""Deferred production observation polling, Future gating and lifetime checks.

Full Session source and real watch helpers; SDK/worker and EventQueue lifetime
operations are injected boundaries. Not a full Future-core/reaper scheduling or
VM constructor proof. Execute only in the concentrated Wi-Fi validation phase.
"""
import unittest
from test_wifi_smartconfig_session import COMPONENT, TYPES, BOUNDARIES, CASES, headers, unit
from test_wireless_control_regression import compile_run
from wireless_vm_fixture import extract


class SmartConfigWatch(unittest.TestCase):
    def test_terminal_ordering_saturation_close_and_retained_budget(self):
        public = (COMPONENT / 'src/modules/wifi_smartconfig/esp32_mquickjs_wifi_smartconfig.c').read_text()
        boundaries = BOUNDARIES.replace('assert(!locks);locks=1;', 'assert(locks<2);locks++;')
        boundaries = boundaries.replace('assert(locks);locks=0;', 'assert(locks);locks--;')
        code = TYPES + headers() + boundaries
        code += unit(COMPONENT / 'src/modules/wifi_smartconfig/esp32_mquickjs_wifi_smartconfig_session.c')
        code += CASES[:CASES.index('int main(void)')]
        code += 'typedef struct mock_queue esp32_mquickjs_event_queue_t;\n'
        code += public[public.index('#define SC_WATCH_HANDLES'):public.index('/* Compare semantic fields')]
        code += QUEUES
        for name in ('sc_watch_same', 'sc_watch_closed', 'sc_watch_destroyed',
                     'esp32_mquickjs_wifi_smartconfig_poll_observations'):
            code += extract(public, name)
        compile_run(self, code + MAIN)


QUEUES = r'''
struct mock_queue {
 sc_watch_source_t *source;sc_watch_event_t last;
 unsigned refs,sends,drops;bool closed,disposed,full;
};
static struct mock_queue q;
static void sc_watch_closed(void *);
static void sc_watch_destroyed(void *);
static bool esp32_mquickjs_event_queue_retain(esp32_mquickjs_event_queue_t *queue){
 assert(locks==1);if(queue->disposed)return false;queue->refs++;return true;
}
static void esp32_mquickjs_event_queue_release(esp32_mquickjs_event_queue_t *queue){assert(!locks && queue->refs);queue->refs--;}
static bool esp32_mquickjs_event_queue_close(esp32_mquickjs_event_queue_t *queue){
 assert(!locks);if(queue->closed)return false;queue->closed=true;sc_watch_closed(queue->source);return true;
}
static void esp32_mquickjs_event_queue_request_close(esp32_mquickjs_event_queue_t *queue){(void)esp32_mquickjs_event_queue_close(queue);}
static bool esp32_mquickjs_event_queue_try_send_from_callback(esp32_mquickjs_event_queue_t *queue,const void *event){
 assert(!locks && queue->refs);queue->sends++;
 if(queue->closed || queue->full){queue->drops++;return false;}
 queue->last=*(const sc_watch_event_t *)event;return true;
}
'''

MAIN = r'''
static void observe(esp32_mquickjs_wifi_smartconfig_session_t *s){
 assert(!s_sc_watch_handles && !s_sc_watch_sources[0]);memset(&q,0,sizeof(q));
 sc_watch_source_t *source=heap_caps_calloc(1,sizeof(*source),1);assert(source);
 assert(esp32_mquickjs_wifi_smartconfig_session_retain(s));
 source->session=s;source->queue=&q;source->context_bound=true;q.source=source;
 s_sc_watch_sources[0]=source;s_sc_watch_handles++;
}
static void destroy_queue(void){
 assert(q.closed && !q.refs);q.disposed=true;sc_watch_destroyed(q.source);q.source=NULL;
 assert(!s_sc_watch_handles && !s_sc_watch_sources[0]);
}
int main(void){
 reset();esp32_mquickjs_wifi_smartconfig_session_t *s=create();activate(s);tick();observe(s);
 assert(esp32_mquickjs_wifi_smartconfig_poll_observations(false) && q.sends==1 && q.last.sequence==1);
 assert(!esp32_mquickjs_wifi_smartconfig_poll_observations(false));
 s->status.worker_busy=true;assert(!esp32_mquickjs_wifi_smartconfig_poll_observations(false));s->status.worker_busy=false;
 assert(esp32_mquickjs_wifi_smartconfig_session_wait_begin(s,false));capture_ready=true;tick();
 assert(s->status.credentials_ready && !esp32_mquickjs_wifi_smartconfig_poll_observations(false) && q.sends==1);
 /* Even the worker's early credentials-ready write cannot bypass the gate. */
 s->status.worker_busy=true;assert(!esp32_mquickjs_wifi_smartconfig_poll_observations(false));s->status.worker_busy=false;
 esp32_mquickjs_wifi_smartconfig_session_wait_end(s,false);
 assert(esp32_mquickjs_wifi_smartconfig_poll_observations(false) && q.last.sequence==2 && !s->status.credentials_consumed);
 assert(esp32_mquickjs_wifi_smartconfig_session_wait_begin(s,true));
 q.full=true;esp32_mquickjs_wifi_smartconfig_session_close(s,false);pump();assert(s->status.retired);
 assert(!esp32_mquickjs_wifi_smartconfig_poll_observations(false));
 esp32_mquickjs_wifi_smartconfig_session_wait_end(s,true);
 assert(esp32_mquickjs_wifi_smartconfig_poll_observations(false) && q.drops==1 && q.source->last.sequence==3);
 q.full=false;assert(!esp32_mquickjs_wifi_smartconfig_poll_observations(false)); /* No terminal replay. */
 assert(esp32_mquickjs_event_queue_close(&q) && s_sc_watch_handles==1 && !s_sc_watch_sources[0]);
 esp32_mquickjs_wifi_smartconfig_session_release(s);assert(!s_sc_handles);destroy_queue();

 reset();s=create();activate(s);tick();observe(s);esp32_mquickjs_wifi_smartconfig_session_release(s);
 assert(!s->status.closing);assert(esp32_mquickjs_event_queue_close(&q));assert(s->status.closing);
 pump();assert(!s_sc_handles);destroy_queue();

 reset();s=create();activate(s);tick();observe(s);q.disposed=true;
 (void)esp32_mquickjs_wifi_smartconfig_poll_observations(true);
 assert(!s_sc_watch_sources[0] && !q.source->session && s_sc_watch_handles==1 && !s->status.closing);
 assert(esp32_mquickjs_event_queue_close(&q));destroy_queue();stop(s);

 reset();s=create();activate(s);tick();observe(s);q.source->last.sequence=UINT32_MAX;
 s->status.connection_reason=7;(void)esp32_mquickjs_wifi_smartconfig_poll_observations(false);
 assert(q.closed && !q.sends && !s_sc_watch_sources[0]);destroy_queue();stop(s);
 assert(allocs==frees && !s_sc_handles && !s_sc_workers);
 return 0;
}
'''
