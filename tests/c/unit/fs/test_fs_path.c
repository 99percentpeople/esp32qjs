#include "utils/esp32_mquickjs_fs_path.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void expect_true(bool condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        exit(1);
    }
}

static void expect_string(const char *actual, const char *expected, const char *message)
{
    if (strcmp(actual, expected) != 0) {
        fprintf(stderr, "FAIL: %s (expected %s, got %s)\n", message, expected, actual);
        exit(1);
    }
}

static void test_resolve_path(void)
{
    char path[128];

    expect_true(esp32_mquickjs_fs_resolve_path("/", "/", path, sizeof(path)), "root resolves");
    expect_string(path, "/", "root stays a single slash");
    expect_true(esp32_mquickjs_fs_resolve_path("/", "index.js", path, sizeof(path)), "root-relative file");
    expect_string(path, "/index.js", "no backing mount prefix");
    expect_true(esp32_mquickjs_fs_resolve_path("/framework", "_sys/a.js", path, sizeof(path)), "volume-relative file");
    expect_string(path, "/framework/_sys/a.js", "captured relative base");
    expect_true(esp32_mquickjs_fs_resolve_path("/framework", "/index.js", path, sizeof(path)), "absolute file");
    expect_string(path, "/index.js", "absolute paths ignore relative base");
    expect_true(esp32_mquickjs_fs_resolve_path("/", "//framework//_sys/../x", path, sizeof(path)), "normalize namespace");
    expect_string(path, "/framework/x", "canonical namespace path");
    expect_true(esp32_mquickjs_fs_resolve_path("/framework", "../index.js", path, sizeof(path)), "parent of mount");
    expect_string(path, "/index.js", "parent reaches namespace root");
    expect_true(!esp32_mquickjs_fs_resolve_path("/", "../escape", path, sizeof(path)), "reject above root");
    expect_true(!esp32_mquickjs_fs_resolve_path("/framework", "/a/../../escape", path, sizeof(path)), "reject normalized escape");
    expect_true(!esp32_mquickjs_fs_resolve_path("/", "file", path, 5), "terminator needs space");
    expect_true(esp32_mquickjs_fs_resolve_path("/", "file", path, 6), "exact bounded result");
    expect_string(path, "/file", "bounded result");
    expect_true(esp32_mquickjs_fs_path_contains("/", "/framework/a"), "root contains every mount");
    expect_true(!esp32_mquickjs_fs_path_contains("/framework", "/framework-other/a"), "match segment boundary");
    expect_true(esp32_mquickjs_fs_mount_child("/", "/framework", path, sizeof(path)), "direct mount entry");
    expect_string(path, "/framework", "direct mount path");
    expect_true(esp32_mquickjs_fs_mount_child("/", "/mnt/data", path, sizeof(path)), "virtual mount parent");
    expect_string(path, "/mnt", "virtual parent path");
    expect_true(esp32_mquickjs_fs_mount_child("/mnt", "/mnt/data", path, sizeof(path)), "nested mount entry");
    expect_string(path, "/mnt/data", "nested mount path");
    expect_true(!esp32_mquickjs_fs_mount_child("/", "/", path, sizeof(path)), "root is not its own child");
    expect_true(!esp32_mquickjs_fs_mount_child("/mnt", "/mnt-other/data", path, sizeof(path)), "ignore sibling prefix");
}

static void test_basename(void)
{
    expect_string(esp32_mquickjs_fs_path_basename("/framework/file.txt"),
                  "file.txt",
                  "basename for file");
    expect_string(esp32_mquickjs_fs_path_basename("/framework"),
                  "framework",
                  "basename for root path");
    expect_string(esp32_mquickjs_fs_path_basename(""),
                  "",
                  "basename for empty string");
}

int main(void)
{
    test_resolve_path();
    test_basename();
    return 0;
}
