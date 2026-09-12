"""Deferred tests of production antenna transactions; SDK/register boundaries injected.

No standalone transaction model. The actual GPIO ownership/write/rollback and
PHY callback code is extracted. Register helpers are the hardware boundary;
target compilation and later hardware tests cover those target-specific accesses.
"""
import importlib.util
import re
import tempfile
import unittest

from test_wifi_config_controls import sdk_types
from test_wireless_control_regression import compile_run
from wireless_vm_fixture import CORE, ROOT, build, extract, run

COMPONENT = ROOT / 'components/esp32_mquickjs'
SOURCE = COMPONENT / 'src/modules/wifi_driver/esp32_mquickjs_wifi_antenna.c'


def declaration(text, kind, name):
    return re.search(r'typedef ' + kind + r' \{[^}]*\} ' + name + ';', text).group(0) + '\n'


def types():
    source = SOURCE.read_text()
    header = (COMPONENT / 'internal/esp32_mquickjs_wifi_radio.h').read_text()
    result = sdk_types('esp32c5/representative', ('esp_phy_ant_config_t', 'esp_phy_ant_gpio_config_t'))
    result += declaration(header, 'struct', 'esp32_mquickjs_wifi_radio_config_result_t')
    result += declaration(header, 'union', 'esp32_mquickjs_wifi_antenna_snapshot_t')
    for name in ('antenna_route_t', 'antenna_pin_t', 'antenna_gpio_owner_t', 'antenna_transaction_t'):
        result += declaration(source, 'struct', name)
    return result


def native_source():
    source = SOURCE.read_text()
    prefix = '#include <assert.h>\n#include <stdbool.h>\n#include <stdint.h>\n#include <stdatomic.h>\n#include <stdlib.h>\n#include <string.h>\ntypedef int esp_err_t;\n'
    body = prefix + types() + BOUNDARIES
    for name in ('esp32_mquickjs_wifi_antenna_fault', 'esp32_mquickjs_wifi_antenna_valid',
                 'antenna_config_equal', 'antenna_gpio_equal', 'antenna_gpio_mask',
                 'antenna_route_equal', 'antenna_owned_pin', 'antenna_write_gpio',
                 'antenna_apply_idle', 'esp32_mquickjs_wifi_antenna_write'):
        body += extract(source, name)
    return body + RESET


class WiFiAntennaWrite(unittest.TestCase):
    def test_target_mux_snapshot_and_restore_preserve_adjacent_pads(self):
        source = SOURCE.read_text()
        for c5 in (False, True):
            with self.subTest(target='esp32c5' if c5 else 'esp32c3'):
                body = '#include <assert.h>\n#include <stdbool.h>\n#include <stdint.h>\n'
                body += f'#define CONFIG_IDF_TARGET_ESP32C5 {int(c5)}\n'
                body += declaration(source, 'struct', 'antenna_route_t')
                body += r'''
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE 1
#define ESP_ERR_INVALID_RESPONSE 2
#define USB_INT_PHY0_DP_GPIO_NUM 13
#define USB_SERIAL_JTAG_PAD_PULL_OVERRIDE 1
#define USB_SERIAL_JTAG_DP_PULLUP 2
static uint32_t usb_register;
#define USB_SERIAL_JTAG_CONF0_REG (&usb_register)
#define REG_READ(address) (*(volatile uint32_t *)(address))
#define REG_WRITE(address, value) (*(volatile uint32_t *)(address) = (value))
static struct {struct {uint32_t val;} pin[29], func_out_sel_cfg[29];} GPIO;
static struct {struct {uint32_t val;} gpio[29];} IO_MUX;
#if CONFIG_IDF_TARGET_ESP32C5
/* The SDK header declares this table, but C5 does not define it. */
extern const uintptr_t GPIO_PIN_MUX_REG[29];
#else
static uintptr_t GPIO_PIN_MUX_REG[29];
#endif
static bool enabled[29], available = true;
typedef struct {bool oe;} gpio_io_config_t;
static int gpio_get_io_config(unsigned pin, gpio_io_config_t *io){io->oe=enabled[pin];return 0;}
static bool antenna_pad_available(unsigned pin){return available;}
#define gpio_ll_output_disable(hw, pin) (enabled[pin] = false)
#define gpio_ll_output_enable(hw, pin) (enabled[pin] = true)
'''
                for name in ('antenna_route_read', 'antenna_route_equal', 'antenna_route_restore'):
                    body += extract(source, name)
                body += r'''
int main(void){
 for(unsigned pin=0;pin<29;pin++){
  IO_MUX.gpio[pin].val=0xa5000000+pin;
#if !CONFIG_IDF_TARGET_ESP32C5
  GPIO_PIN_MUX_REG[pin]=(uintptr_t)&IO_MUX.gpio[pin].val;
#endif
 }
 for(unsigned pin=0;pin<29;pin++){
  GPIO.pin[pin].val=0x50+pin;GPIO.func_out_sel_cfg[pin].val=0x80+pin;
  enabled[pin]=pin%2;usb_register=7;
  antenna_route_t saved=antenna_route_read(pin);
  assert(saved.mux==0xa5000000+pin&&saved.pin==0x50+pin&&saved.output==0x80+pin);
  IO_MUX.gpio[pin].val=0;GPIO.pin[pin].val=0;GPIO.func_out_sel_cfg[pin].val=0;enabled[pin]=false;usb_register=4;
  available=false;assert(antenna_route_restore(pin,&saved)==ESP_ERR_INVALID_STATE);
  assert(IO_MUX.gpio[pin].val==0);
  available=true;assert(antenna_route_restore(pin,&saved)==ESP_OK);
  antenna_route_t restored=antenna_route_read(pin);assert(antenna_route_equal(&saved,&restored));
  for(unsigned other=0;other<29;other++)assert(IO_MUX.gpio[other].val==0xa5000000+other);
 }
}
'''
                compile_run(self, body)

    def test_native_phy_lock_modem_ownership_readback_and_failed_rollback(self):
        compile_run(self, native_source() + CONFIG_MAIN)

    def test_gpio_claim_reorder_release_and_original_route_restoration(self):
        compile_run(self, native_source() + GPIO_MAIN)

    def test_gpio_prevalidation_allocation_partial_write_and_rollback_fault(self):
        compile_run(self, native_source() + GPIO_FAILURE_MAIN)

    def test_radio_zero_owner_admission_and_boot_fault_diagnostics(self):
        header = (COMPONENT / 'internal/esp32_mquickjs_wifi_radio.h').read_text()
        radio = (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
        body = native_source() + declaration(header, 'enum', 'esp32_mquickjs_wifi_radio_driver_state_t')
        body += RADIO_BOUNDARY + extract(radio, 'esp32_mquickjs_wifi_radio_write_antenna') + RADIO_MAIN
        compile_run(self, body)

    def test_sdk_idle_callback_uses_real_phy_access_lock(self):
        path = ROOT / 'scripts/patch_idf_phy_antenna.py'
        spec = importlib.util.spec_from_file_location('antenna_patch_idle', path)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        compile_run(self, r'''
#include <assert.h>
#include <stddef.h>
typedef int esp_err_t;
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_STATE 2
static int s_phy_access_lock,awake,called;
static void _lock_acquire(int *lock){assert(!*lock);*lock=1;}
static void _lock_release(int *lock){assert(*lock);*lock=0;}
static int phy_get_modem_flag(void){assert(s_phy_access_lock);return awake;}
static int apply(void *p){assert(s_phy_access_lock);called++;return *(int*)p;}
''' + module.IDLE_TRANSACTION + r'''
int main(void){int error=-77;
 assert(esp32qjs_phy_antenna_run_idle(NULL,NULL)==1&&!s_phy_access_lock);
 awake=1;assert(esp32qjs_phy_antenna_run_idle(apply,&error)==2&&!called&&!s_phy_access_lock);
 awake=0;assert(esp32qjs_phy_antenna_run_idle(apply,&error)==-77&&called==1&&!s_phy_access_lock);
}
''')

    def test_public_capture_and_success_allocation_finish_before_native_write(self):
        source = SOURCE.read_text()
        driver = (COMPONENT / 'src/modules/wifi_driver/esp32_mquickjs_wifi_driver.c').read_text()
        options = (CORE / 'esp32_mquickjs_options.c').read_text().replace(
            '#include "esp32_mquickjs_options.h"',
            (COMPONENT / 'internal/esp32_mquickjs_options.h').read_text().replace(
                '#include "esp32_mquickjs_types.h"', ''))
        body = 'typedef int esp_err_t;\n' + types() + options + VM_BOUNDARY
        body += extract(source, 'esp32_mquickjs_wifi_antenna_valid')
        body += extract((CORE / 'esp32_mquickjs.c').read_text(), 'esp32_mquickjs_throw_native_error')
        for name in ('driver_antenna_to_js', 'driver_phy_write_error', 'driver_antenna_capture',
                     'driver_write_antenna', 'js_wifi_driver_set_antenna', 'js_wifi_driver_set_antenna_gpio'):
            body += extract(driver, name)
        with tempfile.TemporaryDirectory() as directory:
            binary = build(directory, body, VM_MAIN)
            config = '{rxMode:"auto",txMode:"ant1",rxDefault:"ant0",enabledAnt0:15,enabledAnt1:14}'
            gpios = '{gpios:[{selected:true,gpio:4},{selected:false,gpio:127},{selected:false,gpio:126},{selected:false,gpio:125}]}'
            for gpio, value in ((False, config), (True, gpios)):
                for failure in (False, True):
                    run([str(binary), '(' + value + ')', str(int(gpio)), '1', str(int(failure))])
            for value in ('{}', config.replace('15,', '15.5,'), config.replace('15,', '4294967311,'),
                          config.replace('rxMode:"auto"', 'rxMode:"ant0"').replace('txMode:"ant1"', 'txMode:"auto"'),
                          config[:-1] + ',unknown:1}'):
                run([str(binary), '(' + value + ')', '0', '0', '0'])
            for value in (gpios.replace('true', '1'), gpios.replace('gpio:127', 'gpio:128'), '{gpios:[]}', '{gpios:[null,null,null,null]}'):
                run([str(binary), '(' + value + ')', '1', '0', '0'])
            for count in ('0', '2'):
                run([str(binary), '(' + config + ')', '0', '0', '0', count])


BOUNDARIES = r'''
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_INVALID_RESPONSE 3
#define ESP_ERR_NO_MEM 4
#define CONFIG_ESP32_MQUICKJS_FEATURE_BLE 1
#define CONFIG_BT_ENABLED 1
#define CONFIG_IEEE802154_ENABLED 1
#define ESP_BT_CONTROLLER_STATUS_IDLE 0
#define ESP_IEEE802154_RADIO_DISABLE 0
#define GPIO_IS_VALID_OUTPUT_GPIO(pin) ((pin)<32)
#define PIN_FUNC_GPIO 1
#define SIG_GPIO_OUT_IDX 256
#define GPIO_FUNC0_OUT_INV_SEL 512
static antenna_gpio_owner_t *s_antenna_gpio;
static _Atomic int s_antenna_fault;
static bool ble_closed=true,awake,fail_alloc;
static int controller,ieee154,lock_depth,writes,reads,pin_writes,fail_pin,fail_restore,live;
static int fail_set,fail_get,fail_rollback;
static uint64_t reserved,race_reserved;
static antenna_route_t routes[32];
static bool pad_available[32];
static esp_phy_ant_config_t stored_config;
static esp_phy_ant_gpio_config_t stored_gpio;
static struct {struct {unsigned int_ena,int_type,wakeup_enable;} pin[32];struct{uint32_t val;}func_out_sel_cfg[32];} GPIO;
typedef struct{unsigned fun_sel,ie,oe,sig_out,oe_inv;}gpio_io_config_t;
static bool esp32_mquickjs_ble_phy_idle(void){assert(lock_depth==1);return ble_closed;}
static int esp_bt_controller_get_status(void){assert(lock_depth==1);return controller;}
static int esp_ieee802154_get_state(void){assert(lock_depth==1);return ieee154;}
static int esp32qjs_phy_antenna_run_idle(int(*fn)(void*),void *arg){
 assert(!lock_depth);lock_depth=1;int e=awake?ESP_ERR_INVALID_STATE:fn(arg);lock_depth=0;return e;
}
static int esp_phy_get_ant(esp_phy_ant_config_t *out){assert(lock_depth);*out=stored_config;return ++reads==fail_get?-61:0;}
static int esp_phy_set_ant(esp_phy_ant_config_t *value){
 assert(lock_depth);stored_config=*value;writes++;return writes==fail_set?-62:writes==fail_rollback?-63:0;
}
static int esp_phy_get_ant_gpio(esp_phy_ant_gpio_config_t *out){assert(lock_depth);*out=stored_gpio;return 0;}
static void esp32qjs_phy_ant_gpio_restore_config(const esp_phy_ant_gpio_config_t *value){assert(lock_depth);stored_gpio=*value;}
static unsigned esp32qjs_phy_ant_gpio_signal(unsigned i){static const unsigned s[]={3,8,11,19};return s[i];}
static int gpio_get_io_config(unsigned pin,gpio_io_config_t *io){
 *io=(gpio_io_config_t){.fun_sel=routes[pin].mux,.ie=routes[pin].pin,.oe=routes[pin].enabled,.sig_out=routes[pin].output};return 0;
}
static antenna_route_t antenna_route_read(unsigned pin){return routes[pin];}
static bool antenna_pad_available(unsigned pin){return pad_available[pin];}
static int antenna_route_restore(unsigned pin,const antenna_route_t *value){if((int)pin==fail_restore)return -71;routes[pin]=*value;return 0;}
static bool esp_gpio_is_reserved(uint64_t mask){return !!(reserved&mask);}
static uint64_t esp_gpio_reserve(uint64_t mask){reserved|=race_reserved;uint64_t old=reserved;reserved|=mask;return old;}
static uint64_t esp_gpio_revoke(uint64_t mask){uint64_t old=reserved;reserved&=~mask;return old;}
static int esp32qjs_phy_set_ant_gpio_owned(esp_phy_ant_gpio_config_t *value,uint64_t mask){
 assert(lock_depth&&!(mask&~reserved));writes++;
 for(unsigned i=0;i<4;i++)if(value->gpio_cfg[i].gpio_select){
  unsigned pin=value->gpio_cfg[i].gpio_num;
  routes[pin]=(antenna_route_t){.mux=PIN_FUNC_GPIO,.output=esp32qjs_phy_ant_gpio_signal(i),.enabled=true};
  if(++pin_writes==fail_pin)return -72;
 }
 stored_gpio=*value;return 0;
}
static void *allocate(size_t n){if(fail_alloc)return NULL;void *p=calloc(1,n);assert(p);live++;return p;}
#define esp32_mquickjs_memory_wireless_calloc(owner,n,size,policy,role) allocate((n)*(size))
static void esp32_mquickjs_memory_payload_free(void *p){if(p){assert(live);live--;free(p);}}
'''

RESET = r'''
static void reset(void){
 assert(!lock_depth);esp32_mquickjs_memory_payload_free(s_antenna_gpio);s_antenna_gpio=NULL;
 memset(&stored_gpio,0,sizeof(stored_gpio));memset(&stored_config,0,sizeof(stored_config));memset(&GPIO,0,sizeof(GPIO));
 for(unsigned i=0;i<32;i++){routes[i]=(antenna_route_t){.mux=PIN_FUNC_GPIO,.output=SIG_GPIO_OUT_IDX,.rtc=100+i};pad_available[i]=true;}
 reserved=race_reserved=0;atomic_store(&s_antenna_fault,0);ble_closed=true;awake=fail_alloc=false;
 ieee154=controller=writes=reads=pin_writes=fail_pin=fail_set=fail_get=fail_rollback=0;fail_restore=-1;
}
static esp32_mquickjs_wifi_antenna_snapshot_t gpio_config(unsigned a,unsigned b){
 esp32_mquickjs_wifi_antenna_snapshot_t r={0};r.gpio.gpio_cfg[0].gpio_select=1;r.gpio.gpio_cfg[0].gpio_num=a;
 r.gpio.gpio_cfg[1].gpio_select=1;r.gpio.gpio_cfg[1].gpio_num=b;return r;
}
'''

CONFIG_MAIN = r'''
int main(void){
 esp32_mquickjs_wifi_radio_config_result_t r={0};esp32_mquickjs_wifi_antenna_snapshot_t wanted={0};
 wanted.config.rx_ant_mode=ESP_PHY_ANT_MODE_AUTO;wanted.config.enabled_ant0=15;
 for(int state=0;state<4;state++){
  reset();if(state==0)awake=true;if(state==1)ble_closed=false;if(state==2)controller=1;if(state==3)ieee154=1;
  assert(esp32_mquickjs_wifi_antenna_write(false,&wanted,&r)==ESP_ERR_INVALID_STATE&&!writes&&!reads);
 }
 reset();assert(!esp32_mquickjs_wifi_antenna_write(false,&wanted,&r)&&writes==1&&antenna_config_equal(&stored_config,&wanted.config));
 reset();fail_get=2;r=(esp32_mquickjs_wifi_radio_config_result_t){0};
 assert(esp32_mquickjs_wifi_antenna_write(false,&wanted,&r)==-61&&writes==2&&r.rollback_complete&&!esp32_mquickjs_wifi_antenna_fault());
 assert(stored_config.rx_ant_mode==ESP_PHY_ANT_MODE_ANT0);
 reset();fail_set=1;fail_rollback=2;r=(esp32_mquickjs_wifi_radio_config_result_t){0};
 assert(esp32_mquickjs_wifi_antenna_write(false,&wanted,&r)==-62&&r.rollback_error==-63&&!r.rollback_complete);
 assert(esp32_mquickjs_wifi_antenna_fault()==-63);int before=writes;
 assert(esp32_mquickjs_wifi_antenna_write(false,&wanted,&r)==-63&&writes==before);reset();
}
'''

GPIO_MAIN = r'''
int main(void){
 reset();antenna_route_t original4=routes[4],original5=routes[5],original6=routes[6];
 esp32_mquickjs_wifi_radio_config_result_t r={0};esp32_mquickjs_wifi_antenna_snapshot_t a=gpio_config(4,5);
 assert(!esp32_mquickjs_wifi_antenna_write(true,&a,&r)&&live==1&&reserved==((1ULL<<4)|(1ULL<<5)));
 a=gpio_config(5,4);assert(!esp32_mquickjs_wifi_antenna_write(true,&a,&r)&&live==1);
 assert(routes[5].output==3&&routes[4].output==8);
 a=gpio_config(6,5);assert(!esp32_mquickjs_wifi_antenna_write(true,&a,&r));
 assert(antenna_route_equal(&routes[4],&original4)&&!(reserved&(1ULL<<4)));
 memset(&a,0,sizeof(a));a.gpio.gpio_cfg[3].gpio_num=127;
 assert(!esp32_mquickjs_wifi_antenna_write(true,&a,&r)&&!reserved&&!live&&!s_antenna_gpio);
 assert(antenna_route_equal(&routes[5],&original5)&&antenna_route_equal(&routes[6],&original6));
 assert(stored_gpio.gpio_cfg[3].gpio_num==127);reset();
}
'''

GPIO_FAILURE_MAIN = r'''
int main(void){
 for(int bad=0;bad<8;bad++){
  reset();esp32_mquickjs_wifi_antenna_snapshot_t a=gpio_config(4,5);esp32_mquickjs_wifi_radio_config_result_t r={0};
  if(bad==0)reserved=1ULL<<5;if(bad==1)routes[5].pin=1;if(bad==2)routes[5].enabled=true;
  if(bad==3)pad_available[5]=false;if(bad==4)a.gpio.gpio_cfg[1].gpio_num=4;
  if(bad==5)fail_alloc=true;if(bad==6)race_reserved=1ULL<<5;if(bad==7)a.gpio.gpio_cfg[1].gpio_num=127;
  assert(esp32_mquickjs_wifi_antenna_write(true,&a,&r)!=0&&!writes&&!live&&!s_antenna_gpio);
  assert(reserved==((bad==0||bad==6)?1ULL<<5:0));
 }
 for(int nth=1;nth<=2;nth++){
  reset();antenna_route_t before4=routes[4],before5=routes[5];fail_pin=nth;
  esp32_mquickjs_wifi_antenna_snapshot_t a=gpio_config(4,5);esp32_mquickjs_wifi_radio_config_result_t r={0};
  assert(esp32_mquickjs_wifi_antenna_write(true,&a,&r)==-72&&r.rollback_complete&&!live&&!reserved);
  assert(antenna_route_equal(&routes[4],&before4)&&antenna_route_equal(&routes[5],&before5));
 }
 reset();esp32_mquickjs_wifi_antenna_snapshot_t a=gpio_config(4,5);esp32_mquickjs_wifi_radio_config_result_t r={0};
 assert(!esp32_mquickjs_wifi_antenna_write(true,&a,&r));routes[4].output=99;int before=writes;
 assert(esp32_mquickjs_wifi_antenna_write(true,&a,&r)==ESP_ERR_INVALID_STATE&&writes==before&&routes[4].output==99);
 reset();a=gpio_config(4,5);fail_pin=1;fail_restore=4;r=(esp32_mquickjs_wifi_radio_config_result_t){0};
 assert(esp32_mquickjs_wifi_antenna_write(true,&a,&r)==-72&&r.rollback_error==-71&&!r.rollback_complete);
 assert(esp32_mquickjs_wifi_antenna_fault()==-71&&reserved==((1ULL<<4)|(1ULL<<5)));reset();
}
'''

VM_BOUNDARY = r'''
#define ESP_OK 0
static int native_writes,native_error,write_allocation_count;
static esp32_mquickjs_wifi_antenna_snapshot_t captured;
static int esp32_mquickjs_wifi_radio_write_antenna(bool gpio,const esp32_mquickjs_wifi_antenna_snapshot_t *input,esp32_mquickjs_wifi_radio_config_result_t *result){
 (void)gpio;native_writes++;write_allocation_count=calls;captured=*input;
 *result=(esp32_mquickjs_wifi_radio_config_result_t){.stage="antenna-phy-idle",.error=native_error};return native_error;
}
'''

VM_MAIN = r'''
int main(int argc,char **argv){
 assert(argc==5||argc==6);bool gpio=atoi(argv[2]),valid=atoi(argv[3]);int total=0,arity=argc==6?atoi(argv[5]):1;
 for(int nth=0;nth<=total;nth++){
  void *heap=malloc(96*1024);JSContext *ctx=JS_NewContext(heap,96*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
  JSGCRef input_ref,result_ref;JSValue *input=JS_PushGCRef(ctx,&input_ref),*result=JS_PushGCRef(ctx,&result_ref);
  *input=JS_Eval(ctx,argv[1],strlen(argv[1]),"antenna",JS_EVAL_RETVAL);assert(!JS_IsException(*input));
  calls=native_writes=write_allocation_count=0;fail_at=nth;native_error=atoi(argv[4])?-77:0;collect=true;inject=true;
  *result=gpio?js_wifi_driver_set_antenna_gpio(ctx,NULL,arity,input):js_wifi_driver_set_antenna(ctx,NULL,arity,input);
  inject=false;collect=false;if(!nth)total=calls;
  if(!nth)assert(native_writes==(int)valid);
  if(!native_error&&valid&&native_writes){assert(!JS_IsException(*result)&&calls==write_allocation_count);}
  if(!valid)assert(JS_IsException(*result)&&!native_writes);
  if(JS_IsException(*result)){assert(JS_HasException(ctx));(void)JS_GetException(ctx);}
  JS_PopGCRef(ctx,&result_ref);JS_PopGCRef(ctx,&input_ref);assert(!root_count&&!native_live);JS_FreeContext(ctx);free(heap);
 }
 return 0;
}
'''

RADIO_BOUNDARY = r'''
#define WIFI_RADIO_MAX_LEASES 16
#define ESP_ERR_WIFI_NOT_INIT 5
static int radio_lock;
static struct {
 bool driver_owned,storage_configured,started,stop_required,restart_required,promiscuous_claimed;
 esp32_mquickjs_wifi_radio_driver_state_t driver_state;
 const char *fault_stage,*cleanup_stage;
 struct{unsigned identity;}lifecycle,operation,leases[16];
 unsigned wake_locks;
 esp32_mquickjs_wifi_radio_config_result_t configuration;
}s_radio;
static struct{unsigned identity;bool restore_pending;}s_tx_rate_lease;
#define taskENTER_CRITICAL(lock) ((void)0)
#define taskEXIT_CRITICAL(lock) ((void)0)
static void wifi_radio_operation_lock(void){assert(!radio_lock);radio_lock=1;}
static void wifi_radio_operation_unlock(void){assert(radio_lock);radio_lock=0;}
static int wifi_radio_record_fault(const char *stage,int error){assert(radio_lock);s_radio.fault_stage=stage;return error;}
'''

RADIO_MAIN = r'''
int main(void){
 for(int bad=-1;bad<13;bad++){
  reset();memset(&s_radio,0,sizeof(s_radio));memset(&s_tx_rate_lease,0,sizeof(s_tx_rate_lease));
  s_radio.driver_owned=s_radio.storage_configured=true;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STOPPED;
  switch(bad){
   case 0:s_radio.driver_owned=false;break;case 1:s_radio.storage_configured=false;break;
   case 2:s_radio.started=true;break;case 3:s_radio.stop_required=true;break;
   case 4:s_radio.restart_required=true;break;case 5:s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTED;break;
   case 6:s_radio.fault_stage="older";break;case 7:s_radio.cleanup_stage="cleanup";break;
   case 8:s_radio.lifecycle.identity=7;break;case 9:s_radio.operation.identity=8;break;
   case 10:s_radio.wake_locks=1;break;case 11:s_radio.leases[15].identity=99;break;
   case 12:s_tx_rate_lease.restore_pending=true;break;
  }
  esp32_mquickjs_wifi_antenna_snapshot_t request={0};request.config.enabled_ant1=15;
  esp32_mquickjs_wifi_radio_config_result_t r={0};int error=esp32_mquickjs_wifi_radio_write_antenna(false,&request,&r);
  assert(!radio_lock&&!lock_depth&&s_radio.configuration.error==error);
  if(bad<0)assert(!error&&writes==1);else assert(error&&!writes&&!reads);
 }
 reset();memset(&s_radio,0,sizeof(s_radio));memset(&s_tx_rate_lease,0,sizeof(s_tx_rate_lease));
 s_radio.driver_owned=s_radio.storage_configured=true;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STOPPED;
 esp32_mquickjs_wifi_antenna_snapshot_t request={0};request.config.enabled_ant1=15;fail_set=1;fail_rollback=2;
 esp32_mquickjs_wifi_radio_config_result_t r={0};
 assert(esp32_mquickjs_wifi_radio_write_antenna(false,&request,&r)==-62);
 assert(s_radio.restart_required&&!strcmp(s_radio.fault_stage,"antenna-device-restart-required"));
 assert(s_radio.configuration.error==-62&&s_radio.configuration.rollback_error==-63);reset();
}
'''
