#pragma once

#include <stdbool.h>

typedef int (*esp32_mquickjs_uart_port_create_watch_lock_fn)(
    void *opaque, void **out_lock);
typedef int (*esp32_mquickjs_uart_port_install_driver_fn)(
    void *opaque, void **out_driver_events);
typedef int (*esp32_mquickjs_uart_port_delete_driver_fn)(void *opaque);
typedef void (*esp32_mquickjs_uart_port_delete_watch_lock_fn)(
    void *lock, void *opaque);

typedef struct {
    esp32_mquickjs_uart_port_create_watch_lock_fn create_watch_lock;
    esp32_mquickjs_uart_port_install_driver_fn install_driver;
    esp32_mquickjs_uart_port_delete_driver_fn delete_driver;
    esp32_mquickjs_uart_port_delete_watch_lock_fn delete_watch_lock;
    void *opaque;
} esp32_mquickjs_uart_port_resource_ops_t;

typedef struct {
    void *watch_lock;
    void *driver_events;
    bool driver_installed;
} esp32_mquickjs_uart_port_resources_t;

int esp32_mquickjs_uart_port_resources_init(
    esp32_mquickjs_uart_port_resources_t *resources,
    const esp32_mquickjs_uart_port_resource_ops_t *ops);

int esp32_mquickjs_uart_port_resources_deinit(
    esp32_mquickjs_uart_port_resources_t *resources,
    const esp32_mquickjs_uart_port_resource_ops_t *ops);
