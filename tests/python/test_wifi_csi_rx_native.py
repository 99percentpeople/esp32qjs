"""Deferred production CSI native bridge: task lanes and synchronous identity."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
BASE = ROOT / 'components/esp32_mquickjs'


class WiFiCsiNativeReceipt(unittest.TestCase):
    def test_exact_callback_identity_consumption_nesting_and_bounded_lanes(self):
        for metadata, prefix, c5 in ((48, 44, 0), (64, 56, 1)):
            with self.subTest(metadata=metadata), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                (root / 'freertos').mkdir()
                (root / 'sdkconfig.h').write_text('#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI_CSI 1\n'
                    f'#define CONFIG_IDF_TARGET_ESP32C5 {c5}\n#define METADATA {metadata}\n#define PREFIX {prefix}\n')
                (root / 'esp_wifi.h').write_text('#pragma once\n#include <stdint.h>\n#include <stdbool.h>\n'
                    f'typedef struct {{uint8_t bytes[{metadata}];}} wifi_pkt_rx_ctrl_t;\n'
                    'typedef struct {const void *buf,*hdr;uint32_t len,payload_len;} wifi_csi_info_t;\n')
                (root / 'esp_attr.h').write_text('#define IRAM_ATTR\n')
                (root / 'freertos/FreeRTOS.h').write_text('#pragma once\n#include <pthread.h>\n'
                    'typedef pthread_mutex_t portMUX_TYPE;\n#define portMUX_INITIALIZER_UNLOCKED PTHREAD_MUTEX_INITIALIZER\n'
                    '#define taskENTER_CRITICAL(p) pthread_mutex_lock(p)\n#define taskEXIT_CRITICAL(p) pthread_mutex_unlock(p)\n'
                    'extern int in_isr;\n#define xPortInIsrContext() in_isr\n')
                (root / 'freertos/task.h').write_text('#pragma once\n#include <stdint.h>\n'
                    'typedef void *TaskHandle_t;extern uintptr_t task_id;\n'
                    'static inline TaskHandle_t xTaskGetCurrentTaskHandle(void){return (void *)task_id;}\n')
                source = root / 'native.c'
                source.write_text(CODE)
                binary = root / 'native'
                result = subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-pthread',
                    '-I' + str(root), '-I' + str(BASE / 'internal'), str(source),
                    str(BASE / 'src/modules/wifi_csi/esp32_mquickjs_wifi_csi_rx_native.c'),
                    str(BASE / 'src/modules/wifi_csi/esp32_mquickjs_wifi_csi_rx_span.c'), '-o', str(binary)],
                    capture_output=True, text=True, timeout=30)
                self.assertEqual(result.returncode, 0, result.stderr)
                result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
                self.assertEqual(result.returncode, 0, result.stderr)


CODE = r'''
#include "esp32_mquickjs_wifi_csi_rx_native.h"
#include <assert.h>
#include <string.h>
uintptr_t task_id=1;int in_isr;
static unsigned observed,native_calls;static bool mismatch,nest;static uint32_t last_readable;
void *esp32qjs_wifi_csi_rx_begin_copy(void *,const void *,size_t);
void *esp32qjs_wifi_csi_rx_append_copy(void *,const void *,size_t);
void esp32qjs_wifi_csi_rx_copied(const void *,uint32_t,const void *,const void *,uint32_t,uint32_t);
void wdev_csi_rx_process(const void *samples,uint32_t sample_bytes,const void *rx,const void *header,
    uint32_t reported,uint32_t invalid){
    (void)invalid;native_calls++;
    if(nest){nest=false;esp32qjs_wifi_csi_rx_copied(samples,sample_bytes,rx,header,reported,0);}
    wifi_csi_info_t info={.buf=samples,.len=sample_bytes+(mismatch?1:0),.hdr=header,.payload_len=reported};
    esp32_mquickjs_wifi_csi_rx_packet_t packet;
    if(esp32_mquickjs_wifi_csi_rx_native_take(&info,&packet)){
        observed++;assert(packet.bytes==header);last_readable=(uint32_t)packet.readable_bytes;
    }
    assert(!esp32_mquickjs_wifi_csi_rx_native_take(&info,&packet));
}
static void copy(uint8_t *destination){
    uint8_t input[128]={0};input[METADATA]=8;
    assert(esp32qjs_wifi_csi_rx_begin_copy(destination,input,PREFIX)==destination);
    assert(esp32qjs_wifi_csi_rx_append_copy(destination+METADATA,input+METADATA,17)==destination+METADATA);
    assert(esp32qjs_wifi_csi_rx_append_copy(destination+METADATA+17,input+METADATA+17,23)==destination+METADATA+17);
}
int main(void){
    uint8_t data[6][128]={{0}},samples[4]={0};
    copy(data[0]);esp32qjs_wifi_csi_rx_copied(samples,4,data[0],data[0]+METADATA,40,0);assert(!observed&&native_calls==1);
    esp32_mquickjs_wifi_csi_rx_native_enable(true);
    copy(data[0]);esp32qjs_wifi_csi_rx_copied(samples,4,data[0],data[0]+METADATA,40,0);assert(observed==1&&last_readable==40);
    esp32qjs_wifi_csi_rx_copied(samples,4,data[0],data[0]+METADATA,40,0);assert(observed==1);
    copy(data[0]);mismatch=true;esp32qjs_wifi_csi_rx_copied(samples,4,data[0],data[0]+METADATA,40,0);mismatch=false;assert(observed==1);
    copy(data[0]);esp32qjs_wifi_csi_rx_copied(samples,4,data[0]+1,data[0]+METADATA,40,0);assert(observed==1);
    copy(data[0]);nest=true;esp32qjs_wifi_csi_rx_copied(samples,4,data[0],data[0]+METADATA,40,0);assert(observed==2);
    copy(data[0]);task_id=2;copy(data[1]);task_id=1;
    esp32qjs_wifi_csi_rx_copied(samples,4,data[0],data[0]+METADATA,40,0);
    task_id=2;esp32qjs_wifi_csi_rx_copied(samples,4,data[1],data[1]+METADATA,40,0);assert(observed==4);
    esp32_mquickjs_wifi_csi_rx_native_enable(false);esp32_mquickjs_wifi_csi_rx_native_enable(true);
    for(task_id=1;task_id<=5;task_id++)copy(data[task_id-1]);
    task_id=5;esp32qjs_wifi_csi_rx_copied(samples,4,data[4],data[4]+METADATA,40,0);assert(observed==4);
    esp32_mquickjs_wifi_csi_rx_native_enable(false);task_id=1;in_isr=1;
    esp32_mquickjs_wifi_csi_rx_native_enable(true);copy(data[0]);
    esp32qjs_wifi_csi_rx_copied(samples,4,data[0],data[0]+METADATA,40,0);assert(observed==4);
    in_isr=0;copy(data[0]);esp32_mquickjs_wifi_csi_rx_native_enable(false);
    esp32qjs_wifi_csi_rx_copied(samples,4,data[0],data[0]+METADATA,40,0);assert(observed==4);
    assert(esp32_mquickjs_wifi_csi_rx_native_control_bytes()>0);
    return 0;
}
'''
