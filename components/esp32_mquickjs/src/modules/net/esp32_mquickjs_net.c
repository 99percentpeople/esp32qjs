#include "esp32_mquickjs_net.h"

#if CONFIG_ESP32_MQUICKJS_FEATURE_NET

#include "esp32_mquickjs_core.h"
#include "esp32_mquickjs_event_queue.h"
#include "esp32_mquickjs_memory.h"

#include <stdio.h>
#include <string.h>

#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define NET_EVENT_QUEUE_LEN 4U
#define NET_KEY_MAX_BYTES 63U
#define NET_DESCRIPTION_MAX_BYTES 63U
#define NET_IMPL_NAME_MAX_BYTES 7U
#define NET_IPV4_TEXT_MAX_BYTES 16U
#define NET_IPV6_TEXT_MAX_BYTES 48U

typedef struct {
    char address[NET_IPV4_TEXT_MAX_BYTES];
    char netmask[NET_IPV4_TEXT_MAX_BYTES];
    char gateway[NET_IPV4_TEXT_MAX_BYTES];
} esp32_mquickjs_net_ipv4_snapshot_t;

typedef struct {
    char key[NET_KEY_MAX_BYTES + 1U];
    char description[NET_DESCRIPTION_MAX_BYTES + 1U];
    char name[NET_IMPL_NAME_MAX_BYTES + 1U];
    bool up;
    bool ready;
    bool default_route;
    int route_priority;
    bool has_ipv4;
    esp32_mquickjs_net_ipv4_snapshot_t ipv4;
#if CONFIG_LWIP_IPV6
    char ipv6[CONFIG_LWIP_IPV6_NUM_ADDRESSES][NET_IPV6_TEXT_MAX_BYTES];
#endif
    size_t ipv6_count;
} esp32_mquickjs_net_interface_snapshot_t;

typedef struct {
    bool ready;
    bool truncated;
    bool has_primary;
    char primary_interface[NET_KEY_MAX_BYTES + 1U];
    size_t interface_count;
    esp32_mquickjs_net_interface_snapshot_t
        interfaces[CONFIG_ESP32_MQUICKJS_NET_MAX_INTERFACES];
} esp32_mquickjs_net_snapshot_t;

typedef struct {
    uint32_t generation;
} esp32_mquickjs_net_event_t;

typedef struct esp32_mquickjs_net_watch_source {
    esp32_mquickjs_runtime_t *runtime;
    esp32_mquickjs_event_queue_t *queue;
    struct esp32_mquickjs_net_watch_source *next;
} esp32_mquickjs_net_watch_source_t;

typedef struct {
    bool initialized;
    SemaphoreHandle_t lock;
    esp_event_handler_instance_t ip_event_instance;
    uint32_t generation;
    esp32_mquickjs_net_watch_source_t *sources;
} esp32_mquickjs_net_state_t;

static const char *TAG = "mqjs_net";
static esp32_mquickjs_net_state_t s_net;

static void net_lock(void)
{
    if (s_net.lock != NULL) {
        xSemaphoreTake(s_net.lock, portMAX_DELAY);
    }
}

static void net_unlock(void)
{
    if (s_net.lock != NULL) {
        xSemaphoreGive(s_net.lock);
    }
}

static bool net_try_lock(void)
{
    return s_net.lock != NULL &&
           xSemaphoreTake(s_net.lock, 0) == pdTRUE;
}

static void net_copy_text(char *target, size_t target_size, const char *source)
{
    if (target == NULL || target_size == 0) {
        return;
    }
    snprintf(target, target_size, "%s", source != NULL ? source : "");
}

static bool net_interface_is_ready(esp_netif_t *netif)
{
    esp_netif_ip_info_t ipv4 = {0};

    if (netif == NULL || !esp_netif_is_netif_up(netif)) {
        return false;
    }
    if (esp_netif_get_ip_info(netif, &ipv4) == ESP_OK &&
        ipv4.ip.addr != 0U) {
        return true;
    }
#if CONFIG_LWIP_IPV6
    {
        esp_ip6_addr_t ipv6[CONFIG_LWIP_IPV6_NUM_ADDRESSES];

        return esp_netif_get_all_preferred_ip6(netif, ipv6) > 0;
    }
#else
    return false;
#endif
}

static bool net_ready_predicate(esp_netif_t *netif, void *ctx)
{
    (void)ctx;
    return net_interface_is_ready(netif);
}

static esp_err_t net_collect_snapshot_in_tcpip(void *opaque)
{
    esp32_mquickjs_net_snapshot_t *snapshot = opaque;
    esp_netif_t *primary;
    esp_netif_t *netif = NULL;
    size_t total_count = 0;

    if (snapshot == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    primary = esp_netif_get_default_netif();
    while ((netif = esp_netif_next_unsafe(netif)) != NULL) {
        esp32_mquickjs_net_interface_snapshot_t *entry;
        esp_netif_ip_info_t ipv4 = {0};
        const char *key = esp_netif_get_ifkey(netif);

        total_count++;
        if (netif == primary) {
            snapshot->has_primary = true;
            net_copy_text(snapshot->primary_interface,
                          sizeof(snapshot->primary_interface), key);
        }
        if (snapshot->interface_count >=
            CONFIG_ESP32_MQUICKJS_NET_MAX_INTERFACES) {
            snapshot->ready = snapshot->ready ||
                              net_interface_is_ready(netif);
            continue;
        }
        entry = &snapshot->interfaces[snapshot->interface_count++];
        net_copy_text(entry->key, sizeof(entry->key), key);
        net_copy_text(entry->description, sizeof(entry->description),
                      esp_netif_get_desc(netif));
        if (esp_netif_get_netif_impl_name(netif, entry->name) != ESP_OK) {
            entry->name[0] = '\0';
        }
        entry->up = esp_netif_is_netif_up(netif);
        entry->default_route = netif == primary;
        entry->route_priority = esp_netif_get_route_prio(netif);
        if (esp_netif_get_ip_info(netif, &ipv4) == ESP_OK &&
            ipv4.ip.addr != 0U) {
            entry->has_ipv4 = true;
            (void)esp_ip4addr_ntoa(&ipv4.ip, entry->ipv4.address,
                                   sizeof(entry->ipv4.address));
            (void)esp_ip4addr_ntoa(&ipv4.netmask, entry->ipv4.netmask,
                                   sizeof(entry->ipv4.netmask));
            (void)esp_ip4addr_ntoa(&ipv4.gw, entry->ipv4.gateway,
                                   sizeof(entry->ipv4.gateway));
        }
#if CONFIG_LWIP_IPV6
        {
            esp_ip6_addr_t ipv6[CONFIG_LWIP_IPV6_NUM_ADDRESSES];
            int ipv6_count = esp_netif_get_all_preferred_ip6(netif, ipv6);
            int i;

            if (ipv6_count < 0) {
                ipv6_count = 0;
            } else if (ipv6_count > CONFIG_LWIP_IPV6_NUM_ADDRESSES) {
                ipv6_count = CONFIG_LWIP_IPV6_NUM_ADDRESSES;
            }
            entry->ipv6_count = (size_t)ipv6_count;
            for (i = 0; i < ipv6_count; ++i) {
                snprintf(entry->ipv6[i], sizeof(entry->ipv6[i]), IPV6STR,
                         IPV62STR(ipv6[i]));
            }
        }
#endif
        entry->ready = entry->up &&
                       (entry->has_ipv4 || entry->ipv6_count > 0U);
        snapshot->ready = snapshot->ready || entry->ready;
    }
    snapshot->truncated = total_count > snapshot->interface_count;
    return ESP_OK;
}

static esp_err_t net_collect_snapshot(esp32_mquickjs_net_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(snapshot, 0, sizeof(*snapshot));
    return esp_netif_tcpip_exec(net_collect_snapshot_in_tcpip, snapshot);
}

static JSValue net_make_ipv4(JSContext *ctx,
                             const esp32_mquickjs_net_ipv4_snapshot_t *ipv4)
{
    JSGCRef result_ref;
    JSValue *result = JS_PushGCRef(ctx, &result_ref);

    *result = JS_NewObject(ctx);
    if (JS_IsException(*result) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "address", JS_NewString(ctx, ipv4->address)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "netmask", JS_NewString(ctx, ipv4->netmask)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "gateway", JS_NewString(ctx, ipv4->gateway))) {
        JS_PopGCRef(ctx, &result_ref);
        return JS_EXCEPTION;
    }
    return JS_PopGCRef(ctx, &result_ref);
}

static JSValue net_make_interface(
    JSContext *ctx, const esp32_mquickjs_net_interface_snapshot_t *entry)
{
    JSGCRef result_ref;
    JSGCRef ipv4_ref;
    JSGCRef ipv6_ref;
    JSValue *result = JS_PushGCRef(ctx, &result_ref);
    JSValue *ipv4 = JS_PushGCRef(ctx, &ipv4_ref);
    JSValue *ipv6 = JS_PushGCRef(ctx, &ipv6_ref);
    size_t i;

    *result = JS_NewObject(ctx);
    *ipv4 = entry->has_ipv4 ? net_make_ipv4(ctx, &entry->ipv4) : JS_NULL;
    *ipv6 = JS_NewArray(ctx, 0);
    if (JS_IsException(*result) || JS_IsException(*ipv4) ||
        JS_IsException(*ipv6)) {
        goto fail;
    }
    for (i = 0; i < entry->ipv6_count; ++i) {
        if (JS_IsException(JS_SetPropertyUint32(
                ctx, *ipv6, (uint32_t)i,
#if CONFIG_LWIP_IPV6
                JS_NewString(ctx, entry->ipv6[i])
#else
                JS_NULL
#endif
                ))) {
            goto fail;
        }
    }
    if (!esp32_mquickjs_set_property_ref(
            ctx, result, "key", JS_NewString(ctx, entry->key)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "description",
            JS_NewString(ctx, entry->description)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "name", JS_NewString(ctx, entry->name)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "up", JS_NewBool(entry->up)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "ready", JS_NewBool(entry->ready)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "defaultRoute", JS_NewBool(entry->default_route)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "routePriority",
            JS_NewInt32(ctx, entry->route_priority)) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "ipv4", *ipv4) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "ipv6", *ipv6)) {
        goto fail;
    }
    JS_PopGCRef(ctx, &ipv6_ref);
    JS_PopGCRef(ctx, &ipv4_ref);
    return JS_PopGCRef(ctx, &result_ref);

fail:
    JS_PopGCRef(ctx, &ipv6_ref);
    JS_PopGCRef(ctx, &ipv4_ref);
    JS_PopGCRef(ctx, &result_ref);
    return JS_EXCEPTION;
}

static JSValue net_snapshot_to_js(JSContext *ctx,
                                  const esp32_mquickjs_net_snapshot_t *snapshot)
{
    JSGCRef result_ref;
    JSGCRef interfaces_ref;
    JSValue *result = JS_PushGCRef(ctx, &result_ref);
    JSValue *interfaces = JS_PushGCRef(ctx, &interfaces_ref);
    size_t i;

    *result = JS_NewObject(ctx);
    *interfaces = JS_NewArray(ctx, 0);
    if (JS_IsException(*result) || JS_IsException(*interfaces)) {
        goto fail;
    }
    for (i = 0; i < snapshot->interface_count; ++i) {
        JSValue entry = net_make_interface(ctx, &snapshot->interfaces[i]);

        if (JS_IsException(entry) ||
            JS_IsException(JS_SetPropertyUint32(
                ctx, *interfaces, (uint32_t)i, entry))) {
            goto fail;
        }
    }
    if (!esp32_mquickjs_set_property_ref(
            ctx, result, "ready", JS_NewBool(snapshot->ready)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "primaryInterface",
            snapshot->has_primary
                ? JS_NewString(ctx, snapshot->primary_interface)
                : JS_NULL) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "interfaces", *interfaces) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "truncated", JS_NewBool(snapshot->truncated))) {
        goto fail;
    }
    JS_PopGCRef(ctx, &interfaces_ref);
    return JS_PopGCRef(ctx, &result_ref);

fail:
    JS_PopGCRef(ctx, &interfaces_ref);
    JS_PopGCRef(ctx, &result_ref);
    return JS_EXCEPTION;
}

static JSValue net_make_status(JSContext *ctx)
{
    esp32_mquickjs_net_snapshot_t *snapshot;
    esp_err_t err;
    JSValue result;

    snapshot = esp32_mquickjs_memory_payload_calloc(
        "net.snapshot", 1, sizeof(*snapshot),
        ESP32_MQUICKJS_MEMORY_EXTERNAL);
    if (snapshot == NULL) {
        return JS_ThrowOutOfMemory(ctx);
    }
    err = net_collect_snapshot(snapshot);
    if (err != ESP_OK) {
        esp32_mquickjs_memory_payload_free(snapshot);
        return JS_ThrowInternalError(ctx, "net.status() failed: %s",
                                     esp_err_to_name(err));
    }
    result = net_snapshot_to_js(ctx, snapshot);
    esp32_mquickjs_memory_payload_free(snapshot);
    return result;
}

static JSValue net_event_to_js(JSContext *ctx, const void *event, void *opaque)
{
    JSGCRef result_ref;
    JSGCRef status_ref;
    JSValue *result = JS_PushGCRef(ctx, &result_ref);
    JSValue *status = JS_PushGCRef(ctx, &status_ref);

    (void)event;
    (void)opaque;
    *result = JS_NewObject(ctx);
    *status = net_make_status(ctx);
    if (JS_IsException(*result) || JS_IsException(*status) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "type", JS_NewString(ctx, "status")) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "status", *status)) {
        JS_PopGCRef(ctx, &status_ref);
        JS_PopGCRef(ctx, &result_ref);
        return JS_EXCEPTION;
    }
    JS_PopGCRef(ctx, &status_ref);
    return JS_PopGCRef(ctx, &result_ref);
}

static void net_watch_closed(void *opaque)
{
    esp32_mquickjs_net_watch_source_t *source = opaque;
    esp32_mquickjs_net_watch_source_t **cursor;

    if (source == NULL) {
        return;
    }
    net_lock();
    cursor = &s_net.sources;
    while (*cursor != NULL) {
        if (*cursor == source) {
            *cursor = source->next;
            break;
        }
        cursor = &(*cursor)->next;
    }
    net_unlock();
    heap_caps_free(source);
}

static void net_ip_event_handler(void *arg, esp_event_base_t base,
                                 int32_t event_id, void *event_data)
{
    esp32_mquickjs_net_watch_source_t *source;
    esp32_mquickjs_net_event_t event;

    (void)arg;
    (void)base;
    (void)event_data;
    switch (event_id) {
    case IP_EVENT_STA_GOT_IP:
    case IP_EVENT_STA_LOST_IP:
    case IP_EVENT_GOT_IP6:
    case IP_EVENT_ETH_GOT_IP:
    case IP_EVENT_ETH_LOST_IP:
    case IP_EVENT_PPP_GOT_IP:
    case IP_EVENT_PPP_LOST_IP:
    case IP_EVENT_NETIF_UP:
    case IP_EVENT_NETIF_DOWN:
    case IP_EVENT_CUSTOM_GOT_IP:
    case IP_EVENT_CUSTOM_LOST_IP:
        break;
    default:
        return;
    }
    if (!net_try_lock()) {
        return;
    }
    s_net.generation++;
    if (s_net.generation == 0U) {
        s_net.generation++;
    }
    event.generation = s_net.generation;
    for (source = s_net.sources; source != NULL; source = source->next) {
        (void)esp32_mquickjs_event_queue_try_send_from_callback(
            source->queue, &event);
    }
    net_unlock();
}

esp_err_t esp32_mquickjs_net_ensure_initialized(void)
{
    esp_err_t err;

    if (s_net.lock == NULL) {
        s_net.lock = xSemaphoreCreateMutex();
        if (s_net.lock == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    net_lock();
    if (s_net.initialized) {
        net_unlock();
        return ESP_OK;
    }
    err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_netif_init() failed: %s", esp_err_to_name(err));
        net_unlock();
        return err;
    }
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_event_loop_create_default() failed: %s",
                 esp_err_to_name(err));
        net_unlock();
        return err;
    }
    err = esp_event_handler_instance_register(
        IP_EVENT, ESP_EVENT_ANY_ID, net_ip_event_handler, NULL,
        &s_net.ip_event_instance);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "register IP_EVENT handler failed: %s",
                 esp_err_to_name(err));
        net_unlock();
        return err;
    }
    s_net.initialized = true;
    net_unlock();
    return ESP_OK;
}

bool esp32_mquickjs_net_is_ready(void)
{
    if (esp32_mquickjs_net_ensure_initialized() != ESP_OK) {
        return false;
    }
    return esp_netif_find_if(net_ready_predicate, NULL) != NULL;
}

bool esp32_mquickjs_init_net_runtime(JSContext *ctx,
                                     esp32_mquickjs_runtime_t *runtime)
{
    esp_err_t err;

    (void)runtime;
    err = esp32_mquickjs_net_ensure_initialized();
    if (err != ESP_OK) {
        JS_ThrowInternalError(ctx, "failed to initialize net module: %s",
                              esp_err_to_name(err));
        return false;
    }
    return true;
}

void esp32_mquickjs_deinit_net_runtime(esp32_mquickjs_runtime_t *runtime)
{
    for (;;) {
        esp32_mquickjs_net_watch_source_t *source;
        esp32_mquickjs_event_queue_t *queue = NULL;

        net_lock();
        for (source = s_net.sources; source != NULL; source = source->next) {
            if (source->runtime == runtime) {
                queue = source->queue;
                break;
            }
        }
        net_unlock();
        if (queue == NULL) {
            break;
        }
        (void)esp32_mquickjs_event_queue_close(queue);
    }
}

JSValue js_net_status(JSContext *ctx, JSValue *this_val, int argc,
                      JSValue *argv)
{
    (void)this_val;
    (void)argv;
    if (argc != 0) {
        return JS_ThrowTypeError(ctx, "net.status() expects no arguments");
    }
    return net_make_status(ctx);
}

JSValue js_net_watch(JSContext *ctx, JSValue *this_val, int argc,
                     JSValue *argv)
{
    esp32_mquickjs_runtime_t *runtime = esp32_mquickjs_get_active_runtime();
    esp32_mquickjs_net_watch_source_t *source;
    esp32_mquickjs_net_event_t event;
    JSGCRef queue_ref;
    JSValue *queue_object;

    (void)this_val;
    (void)argv;
    if (argc != 0) {
        return JS_ThrowTypeError(ctx, "net.watch() expects no arguments");
    }
    if (runtime == NULL) {
        return JS_ThrowInternalError(ctx,
                                     "net.watch() requires an active runtime");
    }
    source = heap_caps_calloc(1, sizeof(*source), MALLOC_CAP_8BIT);
    if (source == NULL) {
        return JS_ThrowOutOfMemory(ctx);
    }
    source->runtime = runtime;
    queue_object = JS_PushGCRef(ctx, &queue_ref);
    *queue_object = esp32_mquickjs_event_queue_new(
        ctx, runtime, sizeof(event), NET_EVENT_QUEUE_LEN,
        ESP32_MQUICKJS_EVENT_QUEUE_DROP_NEWEST, net_event_to_js, NULL,
        net_watch_closed, source);
    if (JS_IsException(*queue_object)) {
        heap_caps_free(source);
        JS_PopGCRef(ctx, &queue_ref);
        return JS_EXCEPTION;
    }
    source->queue = esp32_mquickjs_event_queue_from_value(ctx, *queue_object);
    if (source->queue == NULL) {
        JS_PopGCRef(ctx, &queue_ref);
        return JS_ThrowInternalError(ctx,
                                     "net.watch() could not create its event queue");
    }
    net_lock();
    source->next = s_net.sources;
    s_net.sources = source;
    event.generation = s_net.generation;
    net_unlock();
    (void)esp32_mquickjs_event_queue_send(source->queue, &event);
    return JS_PopGCRef(ctx, &queue_ref);
}

#endif
