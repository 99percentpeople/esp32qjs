#include "esp32_mquickjs_memory_owner_accounting.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static void test_same_tuple_aggregates_and_releases(void)
{
    esp32_mquickjs_memory_owner_accounting_t accounting = {0};
    esp32_mquickjs_memory_owner_entry_t entries[4] = {0};
    size_t count;

    assert(esp32_mquickjs_memory_owner_add(
        &accounting, "espnow.rx-pool", 1, 0, 4096, 1));
    assert(esp32_mquickjs_memory_owner_add(
        &accounting, "espnow.rx-pool", 1, 0, 2048, 2));

    count = esp32_mquickjs_memory_owner_snapshot(
        &accounting, entries, 4);
    assert(count == 1);
    assert(strcmp(entries[0].owner, "espnow.rx-pool") == 0);
    assert(entries[0].memory_class == 1);
    assert(entries[0].region == 0);
    assert(entries[0].bytes == 6144);
    assert(entries[0].blocks == 3);

    assert(esp32_mquickjs_memory_owner_remove(
        &accounting, "espnow.rx-pool", 1, 0, 6144, 3));
    assert(esp32_mquickjs_memory_owner_snapshot(
               &accounting, entries, 4) == 0);
}

static void test_class_and_region_are_distinct(void)
{
    esp32_mquickjs_memory_owner_accounting_t accounting = {0};
    esp32_mquickjs_memory_owner_entry_t entries[4] = {0};

    assert(esp32_mquickjs_memory_owner_add(
        &accounting, "bitmap.pixels", 2, 0, 1024, 1));
    assert(esp32_mquickjs_memory_owner_add(
        &accounting, "bitmap.pixels", 2, 1, 4096, 1));
    assert(esp32_mquickjs_memory_owner_add(
        &accounting, "bitmap.pixels", 3, 1, 512, 1));
    assert(esp32_mquickjs_memory_owner_snapshot(
               &accounting, entries, 4) == 3);
}

static void test_invalid_remove_preserves_totals(void)
{
    esp32_mquickjs_memory_owner_accounting_t accounting = {0};
    esp32_mquickjs_memory_owner_entry_t entries[2] = {0};

    assert(esp32_mquickjs_memory_owner_add(
        &accounting, "spi.staging", 4, 0, 32768, 1));
    assert(!esp32_mquickjs_memory_owner_remove(
        &accounting, "spi.staging", 4, 0, 32769, 1));
    assert(!esp32_mquickjs_memory_owner_remove(
        &accounting, "spi.staging", 4, 0, 32768, 2));
    assert(esp32_mquickjs_memory_owner_snapshot(
               &accounting, entries, 2) == 1);
    assert(entries[0].bytes == 32768);
    assert(entries[0].blocks == 1);
}

static void test_snapshot_is_stably_sorted(void)
{
    esp32_mquickjs_memory_owner_accounting_t accounting = {0};
    esp32_mquickjs_memory_owner_entry_t entries[4] = {0};

    assert(esp32_mquickjs_memory_owner_add(
        &accounting, "spi.staging", 4, 0, 1, 1));
    assert(esp32_mquickjs_memory_owner_add(
        &accounting, "espnow.tx-queue", 5, 1, 1, 1));
    assert(esp32_mquickjs_memory_owner_add(
        &accounting, "espnow.tx-queue", 3, 0, 1, 1));

    assert(esp32_mquickjs_memory_owner_snapshot(
               &accounting, entries, 4) == 3);
    assert(strcmp(entries[0].owner, "espnow.tx-queue") == 0);
    assert(entries[0].memory_class == 3);
    assert(strcmp(entries[1].owner, "espnow.tx-queue") == 0);
    assert(entries[1].memory_class == 5);
    assert(strcmp(entries[2].owner, "spi.staging") == 0);
}

static void test_overflow_is_rejected(void)
{
    esp32_mquickjs_memory_owner_accounting_t accounting = {0};
    esp32_mquickjs_memory_owner_entry_t entries[2] = {0};

    assert(esp32_mquickjs_memory_owner_add(
        &accounting, "i2s.driver", 4, 0, SIZE_MAX, UINT32_MAX));
    assert(!esp32_mquickjs_memory_owner_add(
        &accounting, "i2s.driver", 4, 0, 1, 0));
    assert(!esp32_mquickjs_memory_owner_add(
        &accounting, "i2s.driver", 4, 0, 0, 1));
    assert(esp32_mquickjs_memory_owner_snapshot(
               &accounting, entries, 2) == 1);
    assert(entries[0].bytes == SIZE_MAX);
    assert(entries[0].blocks == UINT32_MAX);
}

int main(void)
{
    test_same_tuple_aggregates_and_releases();
    test_class_and_region_are_distinct();
    test_invalid_remove_preserves_totals();
    test_snapshot_is_stably_sorted();
    test_overflow_is_rejected();
    return 0;
}
