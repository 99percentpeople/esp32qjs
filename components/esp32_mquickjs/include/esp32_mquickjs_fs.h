#pragma once

#include "esp32_mquickjs_types.h"

bool esp32_mquickjs_mount_littlefs(bool format_if_mount_failed);

JSValue js_fs_open(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_fs_list(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_fs_stat(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_fs_exists(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_fs_readText(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_fs_writeText(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_fs_appendText(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_fs_remove(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_fs_rename(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_fs_mkdir(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
