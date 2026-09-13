#pragma once
#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>
typedef const char *esp_event_base_t;
typedef void *esp_event_handler_instance_t;
#define WIFI_EVENT ((esp_event_base_t)"WIFI")
#define ESP_EVENT_ANY_ID -1
#define WIFI_EVENT_HOME_CHANNEL_CHANGE 1
#define WIFI_EVENT_STA_CONNECTED 2
#define WIFI_EVENT_AP_START 3
esp_err_t esp_event_loop_create_default(void);
esp_err_t esp_event_handler_instance_register(esp_event_base_t base,int32_t id,void (*handler)(void *,esp_event_base_t,int32_t,void *),void *arg,esp_event_handler_instance_t *instance);

esp_err_t esp_event_handler_instance_unregister(esp_event_base_t base,int32_t id,esp_event_handler_instance_t instance);

#define ESP_EVENT_DEFINE_BASE(name) const char name[] = #name
#define WIFI_EVENT_STA_START 4
#define WIFI_EVENT_STA_STOP 5
#define WIFI_EVENT_AP_STOP 6
esp_err_t esp_event_post(esp_event_base_t base,int32_t id,const void *data,size_t size,unsigned ticks);
