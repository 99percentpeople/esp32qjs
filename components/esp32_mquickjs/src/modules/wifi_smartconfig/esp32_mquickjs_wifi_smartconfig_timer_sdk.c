#include "esp32_mquickjs_wifi_smartconfig_timer.h"
#include "esp32_mquickjs_wifi_chm_timer.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && ((CONFIG_ESP_WIFI_DPP_SUPPORT || CONFIG_ESP_WIFI_NAN_USD_ENABLE) || (CONFIG_ESP_NETIF_USES_TCPIP_WITH_BSD_API && CONFIG_LWIP_IPV4))
#include "esp_attr.h"
#include "rom/ets_sys.h"

/* The reviewed Wi-Fi/coexistence OSI adapters call these legacy entry points.
 * Non-owned timers keep their original implementation, including the existing
 * C5 TWT adapters which intercept earlier in the OSI path. */
void __real_ets_timer_setfn(ETSTimer *timer, ETSTimerFunc *fn, void *argument);
void __real_ets_timer_disarm(ETSTimer *timer);
void __real_ets_timer_done(ETSTimer *timer);
void __real_ets_timer_arm_us(ETSTimer *timer, uint32_t us, bool repeat);
void __real_ets_timer_arm(ETSTimer *timer, uint32_t ms, bool repeat);

void __wrap_ets_timer_setfn(ETSTimer *timer, ETSTimerFunc *fn, void *argument)
{
#if (CONFIG_ESP_WIFI_DPP_SUPPORT || CONFIG_ESP_WIFI_NAN_USD_ENABLE)
    if (esp32qjs_wifi_chm_timer_setfn(timer, fn, argument)) return;
#endif
#if CONFIG_ESP_NETIF_USES_TCPIP_WITH_BSD_API && CONFIG_LWIP_IPV4
    if (esp32_mquickjs_wifi_smartconfig_timer_setfn(timer, fn, argument)) return;
#endif
    __real_ets_timer_setfn(timer, fn, argument);
}
void IRAM_ATTR __wrap_ets_timer_disarm(ETSTimer *timer)
{
#if (CONFIG_ESP_WIFI_DPP_SUPPORT || CONFIG_ESP_WIFI_NAN_USD_ENABLE)
    if (esp32qjs_wifi_chm_timer_disarm(timer)) return;
#endif
#if CONFIG_ESP_NETIF_USES_TCPIP_WITH_BSD_API && CONFIG_LWIP_IPV4
    if (esp32_mquickjs_wifi_smartconfig_timer_disarm(timer)) return;
#endif
    __real_ets_timer_disarm(timer);
}
void __wrap_ets_timer_done(ETSTimer *timer)
{
#if (CONFIG_ESP_WIFI_DPP_SUPPORT || CONFIG_ESP_WIFI_NAN_USD_ENABLE)
    if (esp32qjs_wifi_chm_timer_done(timer)) return;
#endif
#if CONFIG_ESP_NETIF_USES_TCPIP_WITH_BSD_API && CONFIG_LWIP_IPV4
    if (esp32_mquickjs_wifi_smartconfig_timer_done(timer)) return;
#endif
    __real_ets_timer_done(timer);
}
void IRAM_ATTR __wrap_ets_timer_arm_us(ETSTimer *timer, uint32_t us, bool repeat)
{
#if (CONFIG_ESP_WIFI_DPP_SUPPORT || CONFIG_ESP_WIFI_NAN_USD_ENABLE)
    if (esp32qjs_wifi_chm_timer_arm(timer, us, repeat)) return;
#endif
#if CONFIG_ESP_NETIF_USES_TCPIP_WITH_BSD_API && CONFIG_LWIP_IPV4
    if (esp32_mquickjs_wifi_smartconfig_timer_arm(timer, us, repeat)) return;
#endif
    __real_ets_timer_arm_us(timer, us, repeat);
}
#if !(CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_IDF_TARGET_ESP32C5)
/* C5 already owns this wrap in its TWT dispatcher; that dispatcher checks the
 * SmartConfig registry first, without changing the original TWT branches. */
void IRAM_ATTR __wrap_ets_timer_arm(ETSTimer *timer, uint32_t ms, bool repeat)
{
#if (CONFIG_ESP_WIFI_DPP_SUPPORT || CONFIG_ESP_WIFI_NAN_USD_ENABLE)
    if (esp32qjs_wifi_chm_timer_arm(timer, (uint64_t)ms * UINT64_C(1000), repeat)) return;
#endif
#if CONFIG_ESP_NETIF_USES_TCPIP_WITH_BSD_API && CONFIG_LWIP_IPV4
    if (esp32_mquickjs_wifi_smartconfig_timer_arm(timer, (uint64_t)ms * UINT64_C(1000), repeat)) return;
#endif
    __real_ets_timer_arm(timer, ms, repeat);
}
#endif
#endif
