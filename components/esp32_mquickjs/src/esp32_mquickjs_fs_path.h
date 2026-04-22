#pragma once

#include <stdbool.h>
#include <stddef.h>

bool esp32_mquickjs_fs_resolve_path(const char *base_path,
                                    const char *input_path,
                                    char *out_path,
                                    size_t out_path_size);

const char *esp32_mquickjs_fs_path_basename(const char *path);
