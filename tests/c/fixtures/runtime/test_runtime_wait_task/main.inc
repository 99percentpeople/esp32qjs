
int main(void) {
    esp32_mquickjs_async_state_t async={.task_handle=(void *)1};
    esp32qjs_runtime_t host={.task=(void *)1,.watchdog_registered=true,
        .js_watchdog_registered=true,.js_watchdog_user=(void *)3};
    esp32_mquickjs_runtime_t *engine=&host.engine;
    engine->async=&async;engine->cooperate=runtime_cooperate;engine->cooperate_opaque=&host;
    engine->deadline_us=1000;engine->native_wait_depth=1;
    current=(void *)2;now=100;
#if TEST_WAIT
    esp32_mquickjs_native_wait_t worker;
    esp32_mquickjs_native_wait_begin(engine,&worker);
    assert(!worker.active && engine->deadline_us==1000 && engine->native_wait_depth==1);
    now=150;esp32_mquickjs_native_wait_end(engine,&worker);
    assert(engine->deadline_us==1000 && engine->native_wait_depth==1);
    /* Worker cancellation follows runtime control, never another JS exec's deadline. */
    engine->scoped_deadline_us=50;
    assert(esp32_mquickjs_cooperate(engine));
    assert(!task_feeds && !foreign_feeds && !js_feeds);
    current=(void *)1;assert(!esp32_mquickjs_cooperate(engine));
    engine->scoped_deadline_us=0;engine->native_wait_depth=0;
    esp32_mquickjs_native_wait_t outer,inner;
    now=200;esp32_mquickjs_native_wait_begin(engine,&outer);
    assert(outer.active && !engine->deadline_us && engine->native_wait_depth==1);
    now=220;esp32_mquickjs_native_wait_begin(engine,&inner);
    now=230;esp32_mquickjs_native_wait_end(engine,&inner);
    assert(!engine->deadline_us && engine->native_wait_depth==1);
    now=260;esp32_mquickjs_native_wait_end(engine,&outer);
    assert(engine->deadline_us==1060 && !engine->native_wait_depth);
    esp32_mquickjs_native_wait_end(engine,&outer);
    assert(engine->deadline_us==1060 && !engine->native_wait_depth);
    async.task_handle=NULL;
    esp32_mquickjs_native_wait_begin(engine,&worker);
    assert(!worker.active && engine->deadline_us==1060 && !engine->native_wait_depth);
#else
    assert(runtime_cooperate(&host));
    assert(!task_feeds && !foreign_feeds && !js_feeds && !host.last_outer_heartbeat_us);
    due=true;assert(!runtime_cooperate(&host));due=false;
    host.stop_requested=true;assert(!runtime_cooperate(&host));host.stop_requested=false;
    current=(void *)1;engine->native_wait_depth=0;
    assert(runtime_cooperate(&host));assert(task_feeds==1 && !js_feeds);
    engine->native_wait_depth=1;
    assert(runtime_cooperate(&host));assert(task_feeds==2 && js_feeds==1 && host.last_outer_heartbeat_us==100);
    assert(!foreign_feeds);
    host.watchdog_registered=host.js_watchdog_registered=false;
    assert(runtime_cooperate(&host));assert(task_feeds==2 && js_feeds==1);
    assert(!runtime_cooperate(NULL));
#endif
    return 0;
}
