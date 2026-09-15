static void evaluate(JSContext *ctx, const char *script)
{
    JSValue result = JS_Eval(ctx, script, strlen(script), "fs-api-test", 0);
    if (JS_IsException(result)) {
        JSValue error = JS_GetException(ctx);
        JSCStringBuf buffer;
        fprintf(stderr, "%s\n", JS_ToCString(ctx, error, &buffer));
        abort();
    }
}
int main(int argc, char **argv)
{
    assert(argc == 2);
    void *heap = malloc(512 * 1024);
    JSContext *ctx = JS_NewContext(heap, 512 * 1024, &js_stdlib);
    assert(ctx);
    esp32_mquickjs_fs_runtime_t state = {.lock = &runtime};
    runtime.fs_state = &state;
    s_littlefs_mounts[0] = (esp32_mquickjs_littlefs_mount_t){true, false, "workspace", "/"};
    s_littlefs_mounts[1] = (esp32_mquickjs_littlefs_mount_t){true, true, "storage", "/framework"};
    test_ctx = ctx; inject = true; collect = atoi(argv[1]); fail_at = 0;
    evaluate(ctx,
        "function check(value) { if (!value) throw new Error('fs API assertion failed'); }"
        "function invalid(fn) { var caught = false; try { fn(); } catch (error) { caught = true; } check(caught); }"
        "check(fs.ROOT === '/');"
        "var mounts = fs.mounts(); check(mounts.length === 2);"
        "check(mounts[0].root === '/' && mounts[0].partition === 'workspace');"
        "check(mounts[1].root === '/framework' && mounts[1].readOnly === true);"
        "check(mounts[1].type === 'littlefs'); mounts[1].root = '/changed';"
        "check(fs.mounts()[1].root === '/framework');"
        "check(fs.info().totalBytes === 4096); check(fs.info(undefined).root === '/');"
        "var info = fs.info('framework/_sys/' + '../missing.js');"
        "check(info.root === '/framework' && info.partition === 'storage');"
        "check(info.totalBytes === 512 && info.usedBytes === 128 && info.freeBytes === 384);"
        "check(fs.info('/framework-other/missing').root === '/');"
        "invalid(function () { fs.info(3); }); invalid(function () { fs.info('/../../bad'); });"
        "invalid(function () { fs.mounts('/'); });"
        "invalid(function () { fs.watch(3); });"
        "invalid(function () { fs.watch('/../../bad'); });"
        "invalid(function () { fs.watch('/', {capacity: 0}); });"
        "invalid(function () { fs.watch('/', {capacity: 65}); });"
        "invalid(function () { fs.watch('/', {capacity: 1.5}); });"
        "invalid(function () { fs.watch('/', {other: 1}); });"
        "invalid(function () { fs.watch('/', {}, 1); });"
        "fs.watch('src/' + 'nested/..', {capacity: 2});");
    assert(state.sources && !strcmp(state.sources->path, "/src") && captured_capacity == 2);
    evaluate(ctx, "fs.watch();");
    assert(!strcmp(state.sources->path, "/") && captured_capacity == 8);
    evaluate(ctx, "fs.watch(undefined, {capacity: 64});");
    assert(!strcmp(state.sources->path, "/") && captured_capacity == 64);
    while (state.sources) fs_change_queue_closed(state.sources);
    assert(native_live == 0 && root_count == 0);
    s_littlefs_mounts[0].active = false;
    evaluate(ctx, "check(fs.mounts().length === 1); invalid(function () { fs.info(); }); check(fs.info('/framework').readOnly);");
    inject = false;
    JS_FreeContext(ctx);
    free(heap);
    return 0;
}
