#pragma once

#include "esp32_mquickjs_types.h"

typedef enum {
    ESP32_MQUICKJS_FS_CHANGE_WRITE,
    ESP32_MQUICKJS_FS_CHANGE_REMOVE,
    ESP32_MQUICKJS_FS_CHANGE_RENAME,
    ESP32_MQUICKJS_FS_CHANGE_MKDIR,
} esp32_mquickjs_fs_change_kind_t;

#if CONFIG_ESP32_MQUICKJS_FEATURE_FS

void esp32_mquickjs_fs_notify_change(esp32_mquickjs_fs_change_kind_t kind,
                                     const char *path,
                                     const char *to_path);

#else

static inline void esp32_mquickjs_fs_notify_change(
    esp32_mquickjs_fs_change_kind_t kind,
    const char *path,
    const char *to_path)
{
    (void)kind;
    (void)path;
    (void)to_path;
}

#endif
