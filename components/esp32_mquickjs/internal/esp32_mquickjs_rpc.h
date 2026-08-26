#pragma once

#include "esp32_mquickjs_types.h"

JSValue js_rpc_create_codec(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_rpc_codec_constructor(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_rpc_codec_close(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
void js_rpc_codec_finalizer(JSContext *ctx, void *opaque);
JSValue js_rpc_create_decoder(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_rpc_decoder_constructor(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_rpc_decoder_close(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
void js_rpc_decoder_finalizer(JSContext *ctx, void *opaque);
JSValue js_rpc_reset_decoder(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_rpc_decoder_status(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_rpc_feed(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_rpc_encode(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_rpc_bytes(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_rpc_file_source(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_rpc_source_info(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_rpc_adopt_file(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_rpc_status(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);

void esp32_mquickjs_deinit_rpc_runtime(void);
