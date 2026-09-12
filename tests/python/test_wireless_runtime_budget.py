"""Deferred production RTOS storage, Wi-Fi/Future resources and worker bootstrap.

The complete memory manager and actual factories execute together. RTOS creation,
deletion and task admission are fault boundaries, not an ESP32 scheduler/ABI test.
"""
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path
from wireless_vm_fixture import ROOT, CORE, extract


class WirelessRuntimeBudget(unittest.TestCase):
    def test_failed_creation_delete_order_retained_worker_suffix_and_restart(self):
        compiler = shutil.which('cc')
        if compiler is None:
            self.skipTest('C compiler unavailable')
        heap = (ROOT / 'tests/c/test_memory_wireless.c').read_text().split('int main(void)')[0]
        heap = heap.replace('void heap_caps_free(void *data)\n{',
                            'void heap_caps_free(void *data)\n{\n    before_heap_free(data);')
        future = (CORE / 'esp32_mquickjs_future.c').read_text()
        wifi_dir = ROOT / 'components/esp32_mquickjs/src/modules/wifi'
        wifi = (wifi_dir / 'esp32_mquickjs_wifi.c').read_text()
        code = 'static void before_heap_free(void *data);\n' + heap + SHIM
        code += (CORE / 'esp32_mquickjs_memory_rtos.c').read_text()
        for source, functions in [(future, ['future_runtime_resource_allocate',
                'future_runtime_resource_release', 'future_runtime_queue_create', 'future_runtime_queue_delete']),
                (wifi, ['wifi_create_lock', 'wifi_delete_lock', 'wifi_create_event_group',
                        'wifi_delete_event_group', 'wifi_create_queue', 'wifi_delete_queue'])]:
            for name in functions:
                code += extract(source, name)
        for source, tag in [(future, 'future'), (wifi, 'wifi')]:
            a = source.index('static const esp32_mquickjs_' + tag + '_runtime_resource_ops_t')
            code += source[a:source.index('};', a) + 2] + '\n'
        # Use the production worker item/storage declarations and initializer.
        code += re.search(r'typedef struct \{[^}]*\} future_worker_item_t;', future).group(0) + '\n'
        a = future.index('#define ESP32_MQUICKJS_FUTURE_WORKER_STACK_BYTES')
        code += future[a:future.index('static void future_scheduler_snapshot', a)]
        code += 'static QueueHandle_t s_future_worker_queue;\nstatic bool s_future_worker_pool_initialized;\n'
        code += extract(future, 'future_worker_task')
        code += extract(future, 'future_init_worker_pool')
        code += extract(future, 'esp32_mquickjs_submit_background_worker')
        with tempfile.TemporaryDirectory() as temp:
            directory = Path(temp)
            (directory / 'freertos').mkdir()
            for name in ('queue.h', 'semphr.h', 'event_groups.h'):
                (directory / 'freertos' / name).write_text('#pragma once\n')
            config = (ROOT / 'tests/c/memory_stubs/sdkconfig.h').read_text()
            config = config.replace('INTERNAL_BUDGET_BYTES 4096', 'INTERNAL_BUDGET_BYTES 32768')
            config += '\n#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1\n'
            (directory / 'sdkconfig.h').write_text(config)
            path, binary = directory / 'case.c', directory / 'case'
            path.write_text(code + MAIN)
            sources = [CORE / ('esp32_mquickjs_' + n + '.c') for n in
                       ['memory', 'memory_budget', 'memory_owner_accounting', 'memory_dma_accounting',
                        'future_runtime_resources']]
            sources.append(wifi_dir / 'esp32_mquickjs_wifi_runtime_resources.c')
            result = subprocess.run([compiler, '-std=c11', '-pthread', '-Wall', '-Wextra', '-Werror',
                                     '-Wno-unused-function', '-I' + temp,
                                     '-I' + str(ROOT / 'tests/c/memory_stubs'),
                                     '-I' + str(ROOT / 'components/esp32_mquickjs/internal'),
                                     str(path), *map(str, sources), '-o', str(binary)],
                                    capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stderr)


SHIM = r'''
#include "esp32_mquickjs_future_runtime_resources.h"
#include "esp32_mquickjs_wifi_runtime_resources.h"
typedef unsigned UBaseType_t;
typedef struct {unsigned magic,capacity,item_size;uint8_t *bytes;} StaticQueue_t;
typedef StaticQueue_t StaticSemaphore_t,StaticEventGroup_t;
typedef StaticQueue_t *QueueHandle_t,*SemaphoreHandle_t,*EventGroupHandle_t;
typedef uint8_t StackType_t;
typedef struct {unsigned magic;StackType_t *stack;} StaticTask_t;
typedef StaticTask_t *TaskHandle_t;
typedef int esp32_mquickjs_runtime_t;
typedef struct {uint8_t slot;uint32_t generation;} esp32_mquickjs_future_token_t;
typedef void (*esp32_mquickjs_future_worker_fn_t)(void *);
static unsigned native_calls,native_fail,deleted,task_calls,task_fail,errors,submitted;
static struct {void *pointer;bool alive;} native_buffers[32];
static TaskHandle_t tasks[2];
#define CONFIG_ESP32_MQUICKJS_FUTURE_WORKER_POOL_SIZE 2
#define CONFIG_ESP32_MQUICKJS_FUTURE_WORKER_QUEUE_LEN 4
#define portMAX_DELAY UINT32_MAX
#define pdTRUE 1
#define tskIDLE_PRIORITY 0
#define ESP_LOGE(tag,...) do{(void)(tag);++errors;}while(0)
static const char *TAG="fixture";
static bool native_create(void *pointer){
    if(++native_calls==native_fail)return false;
    for(unsigned i=0;i<32;++i)if(!native_buffers[i].pointer){
        native_buffers[i].pointer=pointer;native_buffers[i].alive=true;return true;
    }
    abort();
}
static void before_heap_free(void *pointer){
    if(!pointer)return;
    for(unsigned i=0;i<32;++i)if(native_buffers[i].pointer==pointer){
        assert(!native_buffers[i].alive);native_buffers[i].pointer=NULL;
    }
}
static QueueHandle_t xQueueCreateStatic(unsigned capacity,unsigned size,uint8_t *bytes,StaticQueue_t *control){
    assert(capacity && size && bytes==(uint8_t *)(control+1));
    if(!native_create(control))return NULL;
    *control=(StaticQueue_t){.magic=123,.capacity=capacity,.item_size=size,.bytes=bytes};return control;
}
static void vQueueDelete(QueueHandle_t queue){
    assert(queue->magic==123);queue->magic=0;++deleted;
    for(unsigned i=0;i<32;++i)if(native_buffers[i].pointer==queue){
        assert(native_buffers[i].alive);native_buffers[i].alive=false;return;
    }
    abort();
}
static SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t *control){
    if(!native_create(control))return NULL;
    control->magic=123;return control;
}
#define vSemaphoreDelete vQueueDelete
static EventGroupHandle_t xEventGroupCreateStatic(StaticEventGroup_t *control){return xSemaphoreCreateMutexStatic(control);}
#define vEventGroupDelete vQueueDelete
static bool esp32_mquickjs_future_wake(esp32_mquickjs_runtime_t *r,esp32_mquickjs_future_token_t t){(void)r;(void)t;abort();}
static int xQueueReceive(QueueHandle_t q,void *data,unsigned ticks){(void)q;(void)data;(void)ticks;abort();}
static int xQueueSend(QueueHandle_t q,const void *data,unsigned ticks){assert(q && data && ticks==0);++submitted;return pdTRUE;}
static TaskHandle_t xTaskCreateStatic(void (*function)(void *),const char *name,unsigned depth,void *arg,
    unsigned priority,StackType_t *stack,StaticTask_t *task){
    assert(function && !strcmp(name,"mqjs_io") && depth==4096 && arg==NULL && priority==2);
    if(++task_calls==task_fail)return NULL;
    assert(task->magic==0);task->magic=456;task->stack=stack;
    for(unsigned i=0;i<2;++i)if(!tasks[i]){tasks[i]=task;return task;}
    abort();
}
'''
MAIN = r'''
static void work(void *p){(void)p;abort();}
int main(void){
    esp32_mquickjs_memory_init();
    for(unsigned nth=1;nth<=10;++nth){
        esp32_mquickjs_wifi_runtime_resources_t r;
        allocation_calls=0;fail_at=nth;
        assert(!esp32_mquickjs_wifi_runtime_resources_init(&r,&s_wifi_runtime_resource_ops,2,16,2,16,4,16));
        empty();
    }
    fail_at=0;
    for(unsigned nth=1;nth<=5;++nth){
        esp32_mquickjs_wifi_runtime_resources_t r;native_calls=0;native_fail=nth;
        assert(!esp32_mquickjs_wifi_runtime_resources_init(&r,&s_wifi_runtime_resource_ops,2,16,2,16,4,16));
        empty();
    }
    native_fail=0;unsigned previous=allocation_calls;
    assert(!esp32_mquickjs_memory_queue_create("wifi",SIZE_MAX,16,false));
    assert(!esp32_mquickjs_memory_queue_create("wifi",2,SIZE_MAX,false));
    assert(!esp32_mquickjs_memory_queue_create("wifi",0,16,false));
    assert(previous==allocation_calls);
    esp32_mquickjs_wifi_runtime_resources_t wifi;
    assert(esp32_mquickjs_wifi_runtime_resources_init(&wifi,&s_wifi_runtime_resource_ops,2,16,2,16,4,16));
    assert(status().wireless.regions[0].roles[ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL]>0);
    esp32_mquickjs_wifi_runtime_resources_deinit(&wifi,&s_wifi_runtime_resource_ops);empty();
    void *probe=esp32_mquickjs_memory_wireless_alloc("fill",1,
        ESP32_MQUICKJS_MEMORY_PINNED_INTERNAL,ESP32_MQUICKJS_MEMORY_BUDGET_POOL);assert(probe);
    size_t overhead=status().wireless.regions[0].reserved-1;
    esp32_mquickjs_memory_payload_free(probe);
    void *fill=esp32_mquickjs_memory_wireless_alloc("fill",32768-512-overhead,
        ESP32_MQUICKJS_MEMORY_PINNED_INTERNAL,ESP32_MQUICKJS_MEMORY_BUDGET_POOL);assert(fill);
    assert(!esp32_mquickjs_memory_queue_create("wifi",1,1,false));
    QueueHandle_t control=esp32_mquickjs_memory_queue_create("wifi",1,1,true);assert(control);
    esp32_mquickjs_memory_queue_delete(control);
    esp32_mquickjs_memory_payload_free(fill);empty();
    for(unsigned nth=1;nth<=8;++nth){
        esp32_mquickjs_future_runtime_resources_t r;allocation_calls=0;fail_at=nth;
        assert(!esp32_mquickjs_future_runtime_resources_init(&r,&s_future_runtime_resource_ops,64,4,32,4,8));
        empty();
    }
    fail_at=0;
    for(unsigned nth=1;nth<=4;++nth){
        allocation_calls=0;fail_at=nth;
        assert(!future_init_worker_pool());
        assert(!s_future_workers_created && !s_future_worker_queue && !s_future_worker_storage);
        empty();
    }
    fail_at=0;task_calls=0;task_fail=1;
    assert(!future_init_worker_pool());empty();
    task_calls=0;task_fail=2;
    assert(!future_init_worker_pool());
    assert(s_future_workers_created==1 && tasks[0] && !tasks[1]);
    assert(!esp32_mquickjs_submit_background_worker(work,NULL) && !submitted);
    size_t retained=status().wireless.regions[0].reserved;
    assert(status().wireless.regions[0].roles[ESP32_MQUICKJS_MEMORY_BUDGET_STACK]>=8192);
    QueueHandle_t queue=s_future_worker_queue;TaskHandle_t first=tasks[0];
    previous=allocation_calls;task_fail=0;
    assert(future_init_worker_pool());
    assert(task_calls==3 && allocation_calls==previous && tasks[0]==first && tasks[1]);
    assert(s_future_worker_queue==queue && s_future_workers_created==2);
    assert(esp32_mquickjs_submit_background_worker(work,NULL) && submitted==1);
    for(unsigned cycle=0;cycle<3;++cycle){
        esp32_mquickjs_future_runtime_resources_t r;
        assert(future_init_worker_pool() && task_calls==3);
        assert(esp32_mquickjs_future_runtime_resources_init(&r,&s_future_runtime_resource_ops,64,4,32,4,8));
        esp32_mquickjs_future_runtime_resources_deinit(&r,&s_future_runtime_resource_ops);
        esp32_mquickjs_memory_release_generation();
        assert(status().wireless.regions[0].reserved==retained && tasks[0]==first);
    }
    /* A created pool has boot lifetime. Do not manufacture task retirement in
     * this fixture or release live static stacks to obtain a zero ledger. */
    return 0;
}
'''
