"""Deferred production arbiter tests; only task-lock boundaries are substituted."""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from test_wifi_rx_target import ROOT, INTERNAL, unit


class WiFiRawTxLane(unittest.TestCase):
    def test_fifo_exact_identity_cancellation_capacity_exhaustion_and_concurrency(self):
        compiler = shutil.which('cc')
        if compiler is None:
            self.skipTest('C compiler unavailable')
        code = PRELUDE
        code += unit(INTERNAL / 'esp32_mquickjs_wifi_raw_tx_lane.h')
        code += unit(ROOT / 'components/esp32_mquickjs/src/modules/wifi_raw_tx/esp32_mquickjs_wifi_raw_tx_lane.c')
        with tempfile.TemporaryDirectory() as tmp:
            source, binary = Path(tmp) / 'fixture.c', Path(tmp) / 'fixture'
            source.write_text(code + MAIN)
            built = subprocess.run([compiler, '-std=c11', '-pthread', '-Wall', '-Wextra', '-Werror',
                                    str(source), '-o', str(binary)], capture_output=True, text=True, timeout=30)
            self.assertEqual(built.returncode, 0, built.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stderr)


PRELUDE = r'''
#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <pthread.h>
typedef pthread_mutex_t portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED PTHREAD_MUTEX_INITIALIZER
static _Thread_local unsigned critical_depth;
#define portENTER_CRITICAL(lock) do { assert(!critical_depth); assert(!pthread_mutex_lock(lock)); ++critical_depth; } while(0)
#define portEXIT_CRITICAL(lock) do { --critical_depth; assert(!pthread_mutex_unlock(lock)); } while(0)
'''

MAIN = r'''
#define api(name) esp32_mquickjs_wifi_raw_tx_lane_##name
#define CAP ESP32_MQUICKJS_WIFI_RAW_TX_LANE_CAPACITY
#define OK ESP32_MQUICKJS_WIFI_RAW_TX_LANE_OK
#define FULL ESP32_MQUICKJS_WIFI_RAW_TX_LANE_FULL
#define INVALID ESP32_MQUICKJS_WIFI_RAW_TX_LANE_INVALID
#define EXHAUSTED ESP32_MQUICKJS_WIFI_RAW_TX_LANE_EXHAUSTED
typedef esp32_mquickjs_wifi_raw_tx_lane_token_t token_t;
typedef esp32_mquickjs_wifi_raw_tx_lane_status_t status_t;
static token_t concurrent[CAP];
static bool granted[CAP];
static pthread_mutex_t barrier_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t barrier_condition=PTHREAD_COND_INITIALIZER;
static unsigned arrived;

static void *request_concurrently(void *opaque) {
    size_t index=(size_t)opaque;
    assert(api(request)(&concurrent[index])==OK);
    assert(!pthread_mutex_lock(&barrier_lock));
    ++arrived;
    assert(!pthread_cond_broadcast(&barrier_condition));
    while(arrived<CAP)assert(!pthread_cond_wait(&barrier_condition,&barrier_lock));
    assert(!pthread_mutex_unlock(&barrier_lock));
    /* All requests exist before competing acquire calls. Only the oldest
     * request can win, regardless of thread scheduling after this barrier. */
    granted[index]=api(acquire)(&concurrent[index]);
    assert(!critical_depth);
    return NULL;
}

int main(void) {
    token_t t[CAP]={{0}},extra={0};status_t status;
    assert(api(request)(NULL)==INVALID && !api(acquire)(NULL));
    assert(!api(withdraw)(NULL) && !api(release)(NULL));api(status)(NULL);
    token_t nonempty={.index=1};assert(api(request)(&nonempty)==INVALID);
    for(unsigned i=0;i<CAP;++i) {
        assert(api(request)(&t[i])==OK);
        if(i)assert(t[i].identity>t[i-1].identity);
    }
    assert(api(request)(&extra)==FULL && !extra.identity && !extra.index);
    assert(api(request)(&t[0])==INVALID);
    api(status)(&status);assert(status.waiting==CAP && !status.active_identity && !status.identity_exhausted);
    for(unsigned i=1;i<CAP;++i)assert(!api(acquire)(&t[i]));
    assert(api(acquire)(&t[0]) && api(acquire)(&t[0]));
    assert(!api(withdraw)(&t[0]) && !api(release)(&t[1]));
    token_t stale=t[1];assert(api(withdraw)(&t[1]));
    assert(!t[1].identity && !t[1].index && !api(withdraw)(&stale));
    /* Reuse the storage slot, preserving FIFO order by non-reused identity. */
    assert(api(request)(&extra)==OK && extra.index==stale.index && extra.identity!=stale.identity);
    assert(!api(acquire)(&stale) && !api(release)(&stale) && !api(withdraw)(&stale));
    token_t wrong=extra;wrong.index=UINT8_MAX;assert(!api(acquire)(&wrong));
    token_t old_grant=t[0];assert(api(release)(&t[0]));
    assert(!api(release)(&old_grant) && !api(acquire)(&extra));
    for(unsigned i=2;i<CAP;++i) {
        assert(api(acquire)(&t[i]));api(status)(&status);
        assert(status.active_identity==t[i].identity);
        assert(api(release)(&t[i]));
    }
    assert(api(acquire)(&extra) && api(release)(&extra));
    api(status)(&status);assert(!status.waiting && !status.active_identity);

    pthread_t threads[CAP];
    for(size_t i=0;i<CAP;++i)assert(!pthread_create(&threads[i],NULL,request_concurrently,(void *)i));
    for(unsigned i=0;i<CAP;++i)assert(!pthread_join(threads[i],NULL));
    unsigned winner=CAP,count=0;uint32_t oldest=UINT32_MAX;
    for(unsigned i=0;i<CAP;++i) {
        if(granted[i]){winner=i;++count;}
        if(concurrent[i].identity<oldest)oldest=concurrent[i].identity;
        for(unsigned j=0;j<i;++j)assert(concurrent[j].identity!=concurrent[i].identity);
    }
    assert(count==1 && winner<CAP && concurrent[winner].identity==oldest);
    api(status)(&status);assert(status.active_identity==oldest && status.waiting==CAP-1);
    assert(api(release)(&concurrent[winner]));
    for(unsigned left=CAP-1;left;--left) {
        oldest=UINT32_MAX;winner=CAP;
        for(unsigned i=0;i<CAP;++i)if(concurrent[i].identity && concurrent[i].identity<oldest) {
            oldest=concurrent[i].identity;winner=i;
        }
        assert(winner<CAP && api(acquire)(&concurrent[winner]));
        assert(api(release)(&concurrent[winner]));
    }

    /* Seed only the production identity counter near exhaustion. No reset API
     * exists, and a terminal identity remains valid until its exact release. */
    s_lane.next_identity=UINT32_MAX;
    assert(api(request)(&extra)==OK && extra.identity==UINT32_MAX);
    assert(api(request)(&t[0])==EXHAUSTED && !t[0].identity);
    assert(api(acquire)(&extra));api(status)(&status);
    assert(status.identity_exhausted && status.active_identity==UINT32_MAX);
    stale=extra;assert(api(release)(&extra));
    assert(api(request)(&extra)==EXHAUSTED);
    assert(!api(release)(&stale) && !api(withdraw)(&stale));
    api(status)(&status);assert(status.identity_exhausted && !status.waiting && !status.active_identity);
    assert(!critical_depth);
    return 0;
}
'''
