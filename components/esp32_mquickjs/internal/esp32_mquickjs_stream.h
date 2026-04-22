#pragma once

#include "esp32_mquickjs_types.h"

JSValue js_stream_constructor(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_stream_read(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_stream_write(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_stream_flush(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_stream_close(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_stream_seek(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_stream_tell(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_stream_eof(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);

bool esp32_mquickjs_fs_parse_stream_ref(JSContext *ctx,
                                        JSValue stream_value,
                                        esp32_mquickjs_fs_stream_ref_t *out_ref);

bool esp32_mquickjs_stream_is_stream(JSContext *ctx, JSValue stream_value);

JSValue esp32_mquickjs_stream_open_file(JSContext *ctx,
                                        JSValue global_obj,
                                        const char *path,
                                        const char *mode);

JSValue esp32_mquickjs_stream_open_memory_owned(JSContext *ctx,
                                                JSValue global_obj,
                                                char *data,
                                                size_t data_len);

JSValue esp32_mquickjs_stream_clone(JSContext *ctx,
                                    JSValue global_obj,
                                    JSValue stream_value);

int esp32_mquickjs_stream_read_all_text(JSContext *ctx,
                                        JSValue stream_value,
                                        const char *api_name,
                                        char **out_text,
                                        size_t *out_len);

esp_err_t esp32_mquickjs_stream_close_value(JSContext *ctx, JSValue stream_value);

esp_err_t esp32_mquickjs_fs_stream_read(const esp32_mquickjs_fs_stream_ref_t *ref,
                                        void *buf,
                                        size_t buf_len,
                                        size_t *out_len);

esp_err_t esp32_mquickjs_fs_stream_close(const esp32_mquickjs_fs_stream_ref_t *ref);
