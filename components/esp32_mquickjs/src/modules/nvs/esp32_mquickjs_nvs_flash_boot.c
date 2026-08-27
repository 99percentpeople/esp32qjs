#include "esp32_mquickjs_nvs_flash_boot.h"

#include <stdatomic.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

enum {
    NVS_FLASH_BOOT_UNINITIALIZED = 0,
    NVS_FLASH_BOOT_INITIALIZING,
    NVS_FLASH_BOOT_COMPLETE,
};

static _Atomic int s_nvs_flash_boot_state =
    NVS_FLASH_BOOT_UNINITIALIZED;
static _Atomic int s_nvs_flash_boot_result = ESP_ERR_INVALID_STATE;

esp_err_t esp32_mquickjs_nvs_flash_ensure_initialized(void)
{
    int expected = NVS_FLASH_BOOT_UNINITIALIZED;

    if (atomic_compare_exchange_strong_explicit(
            &s_nvs_flash_boot_state, &expected,
            NVS_FLASH_BOOT_INITIALIZING,
            memory_order_acq_rel, memory_order_acquire)) {
        esp_err_t err = nvs_flash_init();

        atomic_store_explicit(
            &s_nvs_flash_boot_result, err, memory_order_relaxed);
        atomic_store_explicit(
            &s_nvs_flash_boot_state, NVS_FLASH_BOOT_COMPLETE,
            memory_order_release);
        return err;
    }

    while (atomic_load_explicit(
               &s_nvs_flash_boot_state, memory_order_acquire) ==
           NVS_FLASH_BOOT_INITIALIZING) {
        vTaskDelay(1);
    }
    return (esp_err_t)atomic_load_explicit(
        &s_nvs_flash_boot_result, memory_order_relaxed);
}
