static JSValue esp32_mquickjs_eval(JSContext *ctx, esp32_mquickjs_runtime_t *runtime,
                                   const char *source, const char *filename, int flags)
{
    (void)flags;
    if (strcmp(source, "SYSTEM_PARENT") == 0) {
        assert(strcmp(filename, "/framework/_sys/parent.js") == 0);
        assert(strcmp(runtime->load_root, "/framework") == 0);
        assert(load_from_fs(ctx, runtime, runtime->load_root, "/index.js") == 1);
        assert(strcmp(runtime->load_root, "/framework") == 0);
        assert(load_from_fs(ctx, runtime, runtime->load_root, "_sys/child.js") == 1);
    } else if (strcmp(source, "USER_ENTRY") == 0) {
        assert(strcmp(filename, "/index.js") == 0);
        assert(strcmp(runtime->load_root, "/") == 0);
        assert(load_from_fs(ctx, runtime, runtime->load_root, "child.js") == 1);
    } else if (strcmp(source, "USER_CHILD") == 0) {
        assert(strcmp(filename, "/child.js") == 0);
        assert(strcmp(runtime->load_root, "/") == 0);
    } else {
        assert(strcmp(source, "SYSTEM_CHILD") == 0);
        assert(strcmp(filename, "/framework/_sys/child.js") == 0);
        assert(strcmp(runtime->load_root, "/framework") == 0);
    }
    return 1;
}
static void write_file(const char *path, const char *text)
{
    FILE *f = fopen(path, "wb");
    assert(f != NULL);
    assert(fwrite(text, 1, strlen(text), f) == strlen(text));
    assert(fclose(f) == 0);
}
static fs_future_entry_t *find_entry(esp32_mquickjs_future_driver_state_t *state, const char *path)
{
    for (size_t i = 0; i < state->entry_count; ++i)
        if (strcmp(state->entries[i].path, path) == 0) return &state->entries[i];
    return NULL;
}
int main(void)
{
    char temporary[] = "/tmp/esp32qjs-namespace-XXXXXX";
    char path[512];
    JSContext ctx = {0};
    esp32_mquickjs_runtime_t runtime = { .load_root = "/outer", .load_root_depth = 2 };
    esp32_mquickjs_future_driver_state_t list = { .path = "/" };
    assert(mkdtemp(temporary) != NULL);
    snprintf(sandbox_root, sizeof(sandbox_root), "%s", temporary);
    snprintf(path, sizeof(path), "%s/user", temporary); assert(mkdir(path, 0700) == 0);
    snprintf(path, sizeof(path), "%s/system", temporary); assert(mkdir(path, 0700) == 0);
    snprintf(path, sizeof(path), "%s/system/_sys", temporary); assert(mkdir(path, 0700) == 0);
    s_littlefs_mounts[0] = (esp32_mquickjs_littlefs_mount_t){true, false, "/"};
    s_littlefs_mounts[1] = (esp32_mquickjs_littlefs_mount_t){true, true, "/framework"};
    assert(strcmp(esp32_mquickjs_fs_vfs_prefix("/"), "") == 0);
    assert(strcmp(esp32_mquickjs_fs_vfs_prefix("/framework"), "/framework") == 0);
    assert(find_littlefs_mount_by_root("/index.js") == &s_littlefs_mounts[0]);
    assert(find_littlefs_mount_by_root("/framework/a") == &s_littlefs_mounts[1]);
    assert(find_littlefs_mount_by_root("/framework-other/a") == &s_littlefs_mounts[0]);
    assert(fs_mutation_path_error("/index.js", "/framework/a") == EXDEV);
    assert(fs_mutation_path_error("/framework/a", NULL) == EROFS);
    assert(fs_mutation_path_error("/index.js", "/renamed.js") == 0);
    assert(fs_mutation_path_error("/", NULL) == EBUSY);
    s_littlefs_mounts[2] = (esp32_mquickjs_littlefs_mount_t){true, false, "/mnt/data"};
    assert(fs_mutation_path_error("/mnt", NULL) == EBUSY);
    s_littlefs_mounts[2].active = false;
    write_file("/index.js", "USER_ENTRY");
    write_file("/child.js", "USER_CHILD");
    write_file("/framework/_sys/parent.js", "SYSTEM_PARENT");
    write_file("/framework/_sys/child.js", "SYSTEM_CHILD");
    /* A backing file with the mount's name must appear once as the mounted directory. */
    snprintf(path, sizeof(path), "%s/user/framework", temporary);
    FILE *shadow = host_fopen("/framework-shadow", "w"); assert(shadow); fclose(shadow);
    assert(rename(host_path("/framework-shadow"), path) == 0);
    fs_list_namespace(&list);
    assert(list.error_number == 0 && list.entry_count == 3);
    assert(S_ISDIR(find_entry(&list, "/framework")->stat_value.st_mode));
    assert(find_entry(&list, "/index.js") != NULL);
    free(list.entries);
    assert(load_from_fs(&ctx, &runtime, "/", "/framework/_sys/parent.js") == 1);
    assert(strcmp(runtime.load_root, "/outer") == 0 && runtime.load_root_depth == 2);
    assert(load_from_fs(&ctx, &runtime, "/", "/missing.js") == JS_EXCEPTION);
    assert(strcmp(runtime.load_root, "/outer") == 0 && runtime.load_root_depth == 2);
    esp32_mquickjs_event_queue_t all = {0}, scoped = {0};
    esp32_mquickjs_fs_change_source_t scoped_source = {.changes = &scoped, .path = "/src"};
    esp32_mquickjs_fs_change_source_t all_source = {.changes = &all, .path = "/", .next = &scoped_source};
    esp32_mquickjs_fs_runtime_t watch_state = {.lock = &all, .sources = &all_source};
    runtime.fs_state = &watch_state;
    active_runtime = &runtime;
    esp32_mquickjs_fs_notify_change(ESP32_MQUICKJS_FS_CHANGE_WRITE, "/src-other/file", NULL);
    assert(all.count == 1 && scoped.count == 0);
    esp32_mquickjs_fs_notify_change(ESP32_MQUICKJS_FS_CHANGE_WRITE, "/src/file", NULL);
    assert(all.count == 2 && scoped.count == 1);
    assert(strcmp(scoped.events[0].path, "/src/file") == 0);
    esp32_mquickjs_fs_notify_change(ESP32_MQUICKJS_FS_CHANGE_RENAME, "/outside", "/src/in");
    esp32_mquickjs_fs_notify_change(ESP32_MQUICKJS_FS_CHANGE_RENAME, "/src/in", "/outside");
    assert(scoped.count == 3);
    assert(strcmp(scoped.events[1].path, "/outside") == 0);
    assert(strcmp(scoped.events[1].to_path, "/src/in") == 0);
    assert(strcmp(scoped.events[2].to_path, "/outside") == 0);
    assert(scoped.events[2].sequence == 3 && all.events[3].sequence == 4);
    esp32_mquickjs_fs_notify_change(ESP32_MQUICKJS_FS_CHANGE_RENAME, "/src/in", "/src/new");
    assert(scoped.count == 4); /* One event even when both endpoints match. */
    scoped_source.event_sequence = UINT32_MAX;
    esp32_mquickjs_fs_notify_change(ESP32_MQUICKJS_FS_CHANGE_REMOVE, "/src", NULL);
    assert(scoped.count == 5 && scoped.events[4].sequence == 1);
    /* Safe mode can enumerate the system mount without any root filesystem. */
    s_littlefs_mounts[0].active = false;
    assert(find_littlefs_mount_by_root("/index.js") == NULL);
    assert(find_littlefs_mount_by_root("/framework/index.js") == &s_littlefs_mounts[1]);
    snprintf(path, sizeof(path), "%s/user/index.js", temporary); assert(unlink(path) == 0);
    snprintf(path, sizeof(path), "%s/user/child.js", temporary); assert(unlink(path) == 0);
    snprintf(path, sizeof(path), "%s/user/framework", temporary); assert(unlink(path) == 0);
    snprintf(path, sizeof(path), "%s/user", temporary); assert(rmdir(path) == 0);
    memset(&list, 0, sizeof(list)); strcpy(list.path, "/");
    fs_list_namespace(&list);
    assert(list.error_number == 0 && list.entry_count == 1);
    assert(strcmp(list.entries[0].path, "/framework") == 0);
    free(list.entries);
    snprintf(path, sizeof(path), "%s/system/_sys/parent.js", temporary); assert(unlink(path) == 0);
    snprintf(path, sizeof(path), "%s/system/_sys/child.js", temporary); assert(unlink(path) == 0);
    snprintf(path, sizeof(path), "%s/system/_sys", temporary); assert(rmdir(path) == 0);
    snprintf(path, sizeof(path), "%s/system", temporary); assert(rmdir(path) == 0);
    assert(rmdir(temporary) == 0);
    return 0;
}
