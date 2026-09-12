#pragma once

#include "sdkconfig.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
#include "esp_err.h"
#include "esp_netif.h"

/* Serialized runtime or background task. Never call from the default event
 * loop: this waits for that loop. The caller must first stop native producers
 * and retain its lifecycle exclusion until this succeeds. Pending/failed calls
 * retain *netif. Only an actual SDK detach error sets *detach_error (permanent:
 * the pinned SDK frees the driver even when clearing its handle fails).
 * The default event loop and its boot-owned handler must outlive all runtimes. */
esp_err_t esp32_mquickjs_wifi_netif_retire(esp_netif_t **netif,
                                        esp_err_t *detach_error);
#endif
