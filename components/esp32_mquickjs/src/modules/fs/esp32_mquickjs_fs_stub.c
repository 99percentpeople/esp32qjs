#include "esp32_mquickjs.h"

#if !CONFIG_ESP32_MQUICKJS_FEATURE_FS

bool esp32_mquickjs_mount_littlefs(bool format_if_mount_failed,
                                   bool read_only)
{
    (void)format_if_mount_failed;
    (void)read_only;
    return false;
}

bool esp32_mquickjs_mount_littlefs_partition(const char *partition_label,
                                             const char *base_path,
                                             bool format_if_mount_failed,
                                             bool read_only)
{
    (void)partition_label;
    (void)base_path;
    (void)format_if_mount_failed;
    (void)read_only;
    return false;
}

void esp32_mquickjs_unmount_littlefs_partition(const char *partition_label)
{
    (void)partition_label;
}

void esp32_mquickjs_unmount_littlefs(void)
{
}

#endif
