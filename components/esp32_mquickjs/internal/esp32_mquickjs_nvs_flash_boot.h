#pragma once

#include "esp_err.h"

/*
 * Initialize the default NVS partition once for the current MCU boot.
 * Failures are reported verbatim. Framework code must never erase application
 * NVS as an implicit recovery policy.
 */
esp_err_t esp32_mquickjs_nvs_flash_ensure_initialized(void);
