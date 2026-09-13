
int main(void){
 JSContext ctx=0;future_runtime_t state={0};esp32_mquickjs_runtime_t runtime={.state=&state};esp32_mquickjs_future_driver_t driver={0};
 fail_allocation=1;assert(!esp32_mquickjs_future_register_driver(&ctx,&runtime,1,&driver));
 assert(!state.driver_count&&!state.drivers&&!live&&!root_count&&ctx);
 ctx=0;fail_allocation=0;
 for(JSValue i=1;i<=16;++i)assert(esp32_mquickjs_future_register_driver(&ctx,&runtime,i,&driver));
 JSGCRef*first=roots[0];fail_allocation=allocations+1;
 assert(!esp32_mquickjs_future_register_driver(&ctx,&runtime,17,&driver));
 assert(state.driver_count==16&&live==1&&root_count==16&&roots[0]==first&&future_find_driver(&state,1)==&driver);
 ctx=0;fail_allocation=0;assert(esp32_mquickjs_future_register_driver(&ctx,&runtime,17,&driver));
 assert(state.driver_count==17&&live==2);future_clear_driver_registry(&ctx,&state);assert(!root_count&&!live);
}
