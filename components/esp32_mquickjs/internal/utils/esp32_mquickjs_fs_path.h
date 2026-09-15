#pragma once

#include <stdbool.h>
#include <stddef.h>

/* ESP-IDF registers the root filesystem as the fallback (empty) prefix. */
static inline const char *esp32_mquickjs_fs_vfs_prefix(const char *mount_path)
{
    return mount_path[0] == '/' && mount_path[1] == '\0' ? "" : mount_path;
}

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

/* Normalize a namespace path; base_path supplies the base for relative input. */
bool esp32_mquickjs_fs_resolve_path(const char *base_path,
                                    const char *input_path,
                                    char *out_path,
                                    size_t out_path_size);

bool esp32_mquickjs_fs_path_contains(const char *root, const char *path);
bool esp32_mquickjs_fs_mount_child(const char *parent, const char *mount,
                                   char *out, size_t out_size);

const char *esp32_mquickjs_fs_path_basename(const char *path);

#endif
