"""Deferred production SDK replacement; no runtime proof before Wi-Fi tests."""
import importlib.util
import unittest
from test_wifi_config_controls import sdk_types
from test_wireless_control_regression import compile_run
from wireless_vm_fixture import ROOT


def source():
    path = ROOT / 'scripts/patch_idf_phy_antenna.py'
    spec = importlib.util.spec_from_file_location('antenna_gpio_patch', path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return PREFIX + sdk_types('esp32c5/representative', ('esp_phy_ant_gpio_config_t',)) + BOUNDARIES + module.GPIO_WRITE


class WiFiAntennaGpioSdk(unittest.TestCase):
    def test_reserved_mask_and_complete_input_validation_before_mutation(self):
        compile_run(self, source() + r'''
int main(void){
 esp_phy_ant_gpio_config_t config={0};config.gpio_cfg[0].gpio_select=1;config.gpio_cfg[0].gpio_num=4;
 reserved=UINT64_C(1)<<4;
 assert(esp_phy_set_ant_gpio(&config)==ESP_ERR_INVALID_ARG&&!writes&&!routes);
 assert(last_mask==reserved);
 reserved=UINT64_C(1)<<2;
 assert(!esp_phy_set_ant_gpio(&config)&&writes==1&&routes==1);
 assert(last_mask==(UINT64_C(1)<<4)&&s_phy_ant_gpio_config.gpio_cfg[0].gpio_num==4);
 for(int invalid=0;invalid<4;invalid++){
  writes=routes=0;reserved=0;config.gpio_cfg[1].gpio_select=1;
  config.gpio_cfg[1].gpio_num=invalid==0?127:invalid==1?46:invalid==2?4:7;
  if(invalid==3)reserved=UINT64_C(1)<<7;
  assert(esp_phy_set_ant_gpio(&config)==ESP_ERR_INVALID_ARG&&!writes&&!routes);
 }
 assert(esp_phy_set_ant_gpio(NULL)==ESP_ERR_INVALID_ARG);
}
''')

    def test_gpio_error_does_not_connect_failed_pin_or_commit_saved_configuration(self):
        compile_run(self, source() + r'''
int main(void){
 esp_phy_ant_gpio_config_t config={0};
 for(unsigned i=0;i<4;i++){config.gpio_cfg[i].gpio_select=1;config.gpio_cfg[i].gpio_num=4+i;}
 for(int nth=1;nth<=4;nth++){
  writes=routes=0;fail_write=nth;memset(&s_phy_ant_gpio_config,0,sizeof(s_phy_ant_gpio_config));
  assert(esp_phy_set_ant_gpio(&config)==-77&&writes==nth&&routes==nth-1);
  for(unsigned i=0;i<4;i++)assert(!s_phy_ant_gpio_config.gpio_cfg[i].gpio_select);
 }
 writes=routes=fail_write=0;
 assert(!esp_phy_set_ant_gpio(&config)&&writes==4&&routes==4);
 assert(!memcmp(&config,&s_phy_ant_gpio_config,sizeof(config)));
}
''')

    def test_framework_owned_pins_skip_reclaim_and_propagate_output_enable_failure(self):
        compile_run(self, source() + r'''
int main(void){
 esp_phy_ant_gpio_config_t config={0};
 for(unsigned i=0;i<4;i++){config.gpio_cfg[i].gpio_select=1;config.gpio_cfg[i].gpio_num=4+i;reserved|=UINT64_C(1)<<(4+i);}
 assert(esp_phy_set_ant_gpio(&config)==ESP_ERR_INVALID_ARG&&!writes&&!routes);
 for(int nth=1;nth<=4;nth++){
  writes=routes=enables=0;fail_enable=nth;memset(&s_phy_ant_gpio_config,0,sizeof(s_phy_ant_gpio_config));
  assert(esp32qjs_phy_set_ant_gpio_owned(&config,reserved)==-78&&writes==nth&&enables==nth&&routes==nth-1);
  for(unsigned i=0;i<4;i++)assert(!s_phy_ant_gpio_config.gpio_cfg[i].gpio_select);
 }
 writes=routes=enables=fail_enable=0;
 assert(!esp32qjs_phy_set_ant_gpio_owned(&config,reserved)&&enables==4&&routes==4);
 memset(&config,0,sizeof(config));esp32qjs_phy_ant_gpio_restore_config(&config);
 assert(!memcmp(&config,&s_phy_ant_gpio_config,sizeof(config)));
}
''')


PREFIX = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 0x102
typedef int esp_err_t;
'''

BOUNDARIES = r'''
static esp_phy_ant_gpio_config_t s_phy_ant_gpio_config;
static const uint32_t s_phy_ant_sel_sig_idx[4]={3,8,11,19};
typedef struct {int intr_type,mode,pull_down_en,pull_up_en;uint64_t pin_bit_mask;} gpio_config_t;
#define GPIO_INTR_DISABLE 0
#define GPIO_MODE_OUTPUT 2
#define GPIO_MODE_DISABLE 0
#define GPIO_PULLDOWN_DISABLE 0
#define GPIO_PULLUP_DISABLE 0
#define GPIO_IS_VALID_OUTPUT_GPIO(pin) ((pin)<49&&(pin)!=46)
static int writes,routes,fail_write,enables,fail_enable;
static uint64_t reserved,last_mask;
static bool esp_gpio_is_reserved(uint64_t mask){last_mask=mask;return !!(reserved&mask);}
static int gpio_config(const gpio_config_t*c){
 assert((c->mode==GPIO_MODE_OUTPUT||c->mode==GPIO_MODE_DISABLE)&&!c->intr_type&&!c->pull_down_en&&!c->pull_up_en);
 assert(c->pin_bit_mask&&!(c->pin_bit_mask&(c->pin_bit_mask-1)));++writes;return writes==fail_write?-77:0;
}
static int gpio_output_enable(unsigned pin){assert(reserved&(UINT64_C(1)<<pin));return ++enables==fail_enable?-78:0;}
static void esp_rom_gpio_connect_out_signal(uint32_t gpio,uint32_t signal,int invert,int output_invert){
 assert(gpio>=4&&gpio<=7&&signal==s_phy_ant_sel_sig_idx[routes]&&!invert&&!output_invert);++routes;
}
'''
