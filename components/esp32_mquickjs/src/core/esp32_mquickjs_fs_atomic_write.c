#include "esp32_mquickjs_fs_atomic_write.h"

#include <errno.h>

int esp32_mquickjs_fs_atomic_write(
    const char *target_path,
    const char *data,
    size_t data_length,
    char *temp_path,
    size_t temp_path_size,
    const esp32_mquickjs_fs_atomic_write_ops_t *ops,
    void *opaque)
{
    void *handle = NULL;
    int result;

    if (target_path == NULL || (data == NULL && data_length != 0) ||
        temp_path == NULL || temp_path_size == 0 || ops == NULL ||
        ops->open_temp == NULL || ops->write_sync_close == NULL ||
        ops->replace == NULL || ops->remove_temp == NULL) {
        return EINVAL;
    }
    result = ops->open_temp(
        target_path, temp_path, temp_path_size, &handle, opaque);
    if (result != 0) {
        return result;
    }
    if (handle == NULL) {
        ops->remove_temp(temp_path, opaque);
        return EIO;
    }
    result = ops->write_sync_close(handle, data, data_length, opaque);
    if (result == 0) {
        result = ops->replace(temp_path, target_path, opaque);
    }
    if (result != 0) {
        ops->remove_temp(temp_path, opaque);
    }
    return result;
}
