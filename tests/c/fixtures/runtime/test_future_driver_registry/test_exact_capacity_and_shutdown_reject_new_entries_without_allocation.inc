
int main(void){
 JSContext ctx=0;future_runtime_t state={0};esp32_mquickjs_runtime_t runtime={.state=&state};esp32_mquickjs_future_driver_t driver={0};
 for(JSValue i=1;i<=ESP32_MQUICKJS_FUTURE_MAX_DRIVERS;++i)assert(esp32_mquickjs_future_register_driver(&ctx,&runtime,i,&driver));
 unsigned before=allocations;assert(!esp32_mquickjs_future_register_driver(&ctx,&runtime,1000,&driver));
 assert(allocations==before&&state.driver_count==ESP32_MQUICKJS_FUTURE_MAX_DRIVERS);
 future_clear_driver_registry(&ctx,&state);state.shutting_down=true;
 assert(!esp32_mquickjs_future_register_driver(&ctx,&runtime,1,&driver)&&allocations==before&&!root_count&&!live);
}
