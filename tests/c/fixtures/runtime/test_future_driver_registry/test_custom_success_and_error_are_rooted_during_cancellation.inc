
int main(void){
 JSContext ctx=0;esp32_mquickjs_runtime_t runtime={0};setup();callback_result=100;
 assert(future_expire_deadlines(&ctx,&runtime));assert(settled_value==200&&slot.state==FUTURE_STATE_FULFILLED);
 setup();callback_result=JS_EXCEPTION;pending_exception=100;
 assert(future_expire_deadlines(&ctx,&runtime));assert(settled_value==200&&slot.state==FUTURE_STATE_REJECTED&&!root_count);
 setup();driver.on_timeout=NULL;assert(future_expire_deadlines(&ctx,&runtime));
 assert(slot.state==FUTURE_STATE_REJECTED&&generic_errors==1&&!root_count);
}
