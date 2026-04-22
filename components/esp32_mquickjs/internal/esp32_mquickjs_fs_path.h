#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifndef CONFIG_ESP32_MQUICKJS_FEATURE_FS
#if defined(__has_include)
#if __has_include("sdkconfig.h")
#include "sdkconfig.h"
#endif
#endif
#endif

#ifndef CONFIG_ESP32_MQUICKJS_FEATURE_FS
#define CONFIG_ESP32_MQUICKJS_FEATURE_FS 0
#endif

#if CONFIG_ESP32_MQUICKJS_FEATURE_FS

bool esp32_mquickjs_fs_resolve_path(const char *base_path,
                                    const char *input_path,
                                    char *out_path,
                                    size_t out_path_size);

const char *esp32_mquickjs_fs_path_basename(const char *path);

#endif
