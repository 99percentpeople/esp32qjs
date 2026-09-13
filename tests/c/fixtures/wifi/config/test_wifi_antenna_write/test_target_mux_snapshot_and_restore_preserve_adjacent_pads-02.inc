
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
