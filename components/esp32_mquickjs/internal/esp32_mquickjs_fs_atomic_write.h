#pragma once

#include <stddef.h>

typedef struct {
    int (*open_temp)(const char *target_path,
                     char *temp_path,
                     size_t temp_path_size,
                     void **out_handle,
                     void *opaque);
    int (*write_sync_close)(void *handle,
                            const char *data,
                            size_t data_length,
                            void *opaque);
    int (*replace)(const char *temp_path,
                   const char *target_path,
                   void *opaque);
    void (*remove_temp)(const char *temp_path, void *opaque);
} esp32_mquickjs_fs_atomic_write_ops_t;

/**
 * Run one atomic file replacement. Once a temp file has been opened, every
 * failure path removes it. The target is replaced only after write, flush,
 * fsync, and close have all succeeded in the adapter.
 */
int esp32_mquickjs_fs_atomic_write(
    const char *target_path,
    const char *data,
    size_t data_length,
    char *temp_path,
    size_t temp_path_size,
    const esp32_mquickjs_fs_atomic_write_ops_t *ops,
    void *opaque);
