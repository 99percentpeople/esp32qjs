#pragma once

#include "esp32_mquickjs.h"

void esp32qjs_runtime_run(JSContext *ctx,
                          esp32_mquickjs_runtime_t *runtime);
