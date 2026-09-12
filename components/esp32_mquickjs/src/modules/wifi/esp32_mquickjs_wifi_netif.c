#include "esp32_mquickjs_wifi_wait.h"
#include "esp32_mquickjs_wifi_netif.h"

#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
#include "esp_event.h"
#include "esp_wifi_default.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <stdatomic.h>
#include <stdint.h>

ESP_EVENT_DEFINE_BASE(ESP32QJS_WIFI_NETIF_EVENT);
#define WIFI_NETIF_RETIRE_TIMEOUT_MS 1000

typedef enum {
    NETIF_IDLE, NETIF_DETACH_QUEUED, NETIF_DETACH_RUNNING,
    NETIF_DETACHED, NETIF_FENCE_QUEUED, NETIF_DONE,
} wifi_netif_phase_t;
typedef struct { uint32_t identity; } wifi_netif_event_t;

/* One boot-owned slot, no JS/runtime/caller-stack pointers. A timeout does not
 * cancel or recycle it. Caller serialization is separate from callback state:
 * the event loop never waits for the runtime task's mutex. */
static struct {
    esp_netif_t *netif;
    uint32_t identity;
    wifi_netif_phase_t phase;
    esp_err_t detach_error;
} s_retire;
static portMUX_TYPE s_retire_lock = portMUX_INITIALIZER_UNLOCKED;
static StaticSemaphore_t s_caller_mutex_storage;
static SemaphoreHandle_t s_caller_mutex;
static atomic_int s_caller_mutex_state;
static esp_event_handler_instance_t s_retire_handler;

static void wifi_netif_event_handler(void *arg, esp_event_base_t base,
                                    int32_t id, void *data)
{
    esp_netif_t *netif = NULL;
    wifi_netif_event_t *event = data;
    (void)arg;
    if (base != ESP32QJS_WIFI_NETIF_EVENT || event == NULL) return;
    portENTER_CRITICAL(&s_retire_lock);
    if (s_retire.identity == event->identity) {
        if (id == NETIF_DETACH_QUEUED && s_retire.phase == NETIF_DETACH_QUEUED) {
            s_retire.phase = NETIF_DETACH_RUNNING;
            netif = s_retire.netif;
        } else if (id == NETIF_FENCE_QUEUED && s_retire.phase == NETIF_FENCE_QUEUED) {
            s_retire.phase = NETIF_DONE;
        }
    }
    portEXIT_CRITICAL(&s_retire_lock);
    if (netif == NULL) return;

    /* Like the SDK's default STOP handler, retire the stack on this event task.
     * This also handles failed setup and a STOP event not yet dispatched. Stop
     * may append IP events; keep the netif alive through a subsequent fence.
     * Shared STA/AP handlers cannot run concurrently with this detach. */
    esp_netif_action_stop(netif, base, id, NULL);
    esp_err_t err = esp_wifi_clear_default_wifi_driver_and_handlers(netif);
    portENTER_CRITICAL(&s_retire_lock);
    s_retire.detach_error = err;
    s_retire.phase = err == ESP_OK ? NETIF_DETACHED : NETIF_DONE;
    portEXIT_CRITICAL(&s_retire_lock);
}

static esp_err_t wifi_netif_retire_locked(esp_netif_t **netif,
                                        esp_err_t *detach_error)
{
    esp_err_t err;
    TickType_t start = xTaskGetTickCount();
    TickType_t timeout = esp32_mquickjs_wifi_wait_remaining(pdMS_TO_TICKS(WIFI_NETIF_RETIRE_TIMEOUT_MS));
    if (*detach_error != ESP_OK) return *detach_error;
    if (*netif == NULL) return ESP_OK;
    if (s_retire_handler == NULL) {
        err = esp_event_handler_instance_register(ESP32QJS_WIFI_NETIF_EVENT,
            ESP_EVENT_ANY_ID, wifi_netif_event_handler, NULL, &s_retire_handler);
        if (err != ESP_OK) return err;
    }
    for (;;) {
        wifi_netif_event_t event;
        wifi_netif_phase_t post = NETIF_IDLE;
        portENTER_CRITICAL(&s_retire_lock);
        if (s_retire.phase == NETIF_IDLE) {
            if (s_retire.identity == UINT32_MAX) {
                portEXIT_CRITICAL(&s_retire_lock);
                return ESP_ERR_INVALID_STATE;
            }
            s_retire.identity++;
            s_retire.netif = *netif;
            s_retire.detach_error = ESP_OK;
            post = s_retire.phase = NETIF_DETACH_QUEUED;
        } else if (s_retire.netif != *netif) {
            portEXIT_CRITICAL(&s_retire_lock);
            return ESP_ERR_INVALID_STATE;
        } else if (s_retire.phase == NETIF_DONE) {
            err = s_retire.detach_error;
            s_retire.netif = NULL;
            s_retire.phase = NETIF_IDLE;
            portEXIT_CRITICAL(&s_retire_lock);
            *detach_error = err;
            if (err == ESP_OK) {
                esp_netif_destroy(*netif);
                *netif = NULL;
            }
            return err;
        } else if (s_retire.phase == NETIF_DETACHED) {
            post = s_retire.phase = NETIF_FENCE_QUEUED;
        }
        event.identity = s_retire.identity;
        portEXIT_CRITICAL(&s_retire_lock);
        if (post != NETIF_IDLE) {
            /* Never block the event-loop producer on a full queue. Rejected
             * admission has no callback; retry only this unsubmitted suffix. */
            err = esp_event_post(ESP32QJS_WIFI_NETIF_EVENT, post,
                                &event, sizeof(event), 0);
            if (err != ESP_OK) {
                portENTER_CRITICAL(&s_retire_lock);
                s_retire.phase = post == NETIF_DETACH_QUEUED ? NETIF_IDLE : NETIF_DETACHED;
                if (s_retire.phase == NETIF_IDLE) s_retire.netif = NULL;
                portEXIT_CRITICAL(&s_retire_lock);
                return err;
            }
        }
        if ((TickType_t)(xTaskGetTickCount() - start) >= timeout)
            return ESP_ERR_TIMEOUT;
        vTaskDelay(1);
    }
}

esp_err_t esp32_mquickjs_wifi_netif_retire(esp_netif_t **netif,
                                        esp_err_t *detach_error)
{
    if (netif == NULL || detach_error == NULL) return ESP_ERR_INVALID_ARG;
    int expected = 0;
    if (atomic_compare_exchange_strong(&s_caller_mutex_state, &expected, 1)) {
        s_caller_mutex = xSemaphoreCreateMutexStatic(&s_caller_mutex_storage);
        atomic_store_explicit(&s_caller_mutex_state, 2, memory_order_release);
    } else if (atomic_load_explicit(&s_caller_mutex_state, memory_order_acquire) != 2) {
        return ESP_ERR_TIMEOUT;
    }
    if (xSemaphoreTake(s_caller_mutex, 0) != pdTRUE) return ESP_ERR_TIMEOUT;
    esp_err_t err = wifi_netif_retire_locked(netif, detach_error);
    (void)xSemaphoreGive(s_caller_mutex);
    return err;
}
#endif
