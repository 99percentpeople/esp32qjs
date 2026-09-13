
int main(void){
 JSContext ctx=0;future_runtime_t state={0};esp32_mquickjs_runtime_t runtime={.state=&state};
 esp32_mquickjs_future_driver_t driver={0},different={1};
 for(JSValue i=1;i<=16;++i)assert(esp32_mquickjs_future_register_driver(&ctx,&runtime,i,&driver));
 JSGCRef*first=roots[0];assert(live==1&&first->val==1);
 for(JSValue i=17;i<=154;++i)assert(esp32_mquickjs_future_register_driver(&ctx,&runtime,i,&driver));
 assert(state.driver_count==154&&root_count==154&&live==10&&roots[0]==first&&first->val==1);
 assert(future_find_driver(&state,154)==&driver);
 assert(esp32_mquickjs_future_register_driver(&ctx,&runtime,1,&driver));
 assert(!esp32_mquickjs_future_register_driver(&ctx,&runtime,1,&different)&&state.driver_count==154);
 future_clear_driver_registry(&ctx,&state);assert(!roots[0]&&!root_count&&!live&&!state.drivers&&!state.driver_count);
}
