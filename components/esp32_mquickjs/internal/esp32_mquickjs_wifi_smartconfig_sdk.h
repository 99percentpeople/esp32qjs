#pragma once
#include "sdkconfig.h"

#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_NETIF_USES_TCPIP_WITH_BSD_API && CONFIG_LWIP_IPV4
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_smartconfig.h"

/* The Session keeps this lane reserved even after its task completes. A failed
 * start may still issue a receipt; retain it and do not automatically resend. */
esp_err_t esp32qjs_smartconfig_ack_reserve(uint64_t owner);
esp_err_t esp32qjs_smartconfig_ack_release(uint64_t owner);
esp_err_t esp32qjs_smartconfig_ack_start(uint64_t owner, smartconfig_type_t type,
    uint8_t token, const uint8_t *cellphone_ip, uint64_t *receipt);
esp_err_t esp32qjs_smartconfig_ack_stop(uint64_t owner, uint64_t identity);

/* Internal fixed-SDK ACK snapshot. No credentials. Identity is boot-scoped,
 * non-reused and independent of any future SmartConfig Session identity.
 * busy=false proves socket/argument retirement, not native decoder or queued
 * SC_EVENT retirement. completed describes local UDP submission only, never
 * receipt by a phone. A stop request alone does not make busy false.
 * All optional outputs and the returned identity are one locked snapshot. */
uint64_t esp32qjs_smartconfig_ack_status(bool *busy, bool *stopping,
    bool *completed, esp_err_t *error, esp_err_t *observation_error,
    int *socket_errno);
#endif
