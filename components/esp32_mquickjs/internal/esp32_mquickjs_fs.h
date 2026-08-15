#pragma once

#include "esp32_mquickjs_types.h"

#if CONFIG_ESP32_MQUICKJS_FEATURE_FS

bool esp32_mquickjs_init_fs_runtime(JSContext *ctx,
                                    esp32_mquickjs_runtime_t *runtime);

bool esp32_mquickjs_mount_littlefs(bool format_if_mount_failed);
bool esp32_mquickjs_mount_littlefs_partition(const char *partition_label,
                                             const char *base_path,
                                             bool format_if_mount_failed);
void esp32_mquickjs_unmount_littlefs_partition(const char *partition_label);

JSValue js_fs_open(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_fs_info(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_fs_list(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_fs_stat(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_fs_exists(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_fs_readText(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_fs_writeText(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_fs_appendText(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_fs_remove(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_fs_rename(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_fs_mkdir(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);

#endif
