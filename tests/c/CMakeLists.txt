cmake_minimum_required(VERSION 3.16)

project(esp32qjs_host_tests C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

enable_testing()

add_executable(test_repl_logic
    test_repl_logic.c
    ../../components/esp32qjs_interactive/src/repl_line.c
)
target_include_directories(test_repl_logic PRIVATE ../../components/esp32qjs_interactive/internal)
add_test(NAME test_repl_logic COMMAND test_repl_logic)

add_executable(test_fs_path
    test_fs_path.c
    ../../components/esp32_mquickjs/src/modules/fs/esp32_mquickjs_fs_path.c
)
target_compile_definitions(test_fs_path PRIVATE CONFIG_ESP32_MQUICKJS_FEATURE_FS=1)
target_include_directories(test_fs_path PRIVATE ../../components/esp32_mquickjs/internal)
target_include_directories(test_fs_path PRIVATE ../../components/esp32_mquickjs/include)
add_test(NAME test_fs_path COMMAND test_fs_path)

add_executable(test_fs_atomic_write
    test_fs_atomic_write.c
    ../../components/esp32_mquickjs/src/core/esp32_mquickjs_fs_atomic_write.c
)
target_include_directories(test_fs_atomic_write PRIVATE ../../components/esp32_mquickjs/internal)
add_test(NAME test_fs_atomic_write COMMAND test_fs_atomic_write)

set(LITTLEFS_SOURCE_DIR ../../managed_components/joltwallet__littlefs/src/littlefs)
add_executable(test_littlefs_recovery
    test_littlefs_recovery.c
    ${LITTLEFS_SOURCE_DIR}/lfs.c
    ${LITTLEFS_SOURCE_DIR}/lfs_util.c
    ${LITTLEFS_SOURCE_DIR}/bd/lfs_rambd.c
)
target_include_directories(test_littlefs_recovery PRIVATE ${LITTLEFS_SOURCE_DIR})
target_compile_definitions(test_littlefs_recovery PRIVATE LFS_NO_DEBUG LFS_NO_WARN)
add_test(NAME test_littlefs_recovery COMMAND test_littlefs_recovery)

add_executable(test_byte_span
    test_byte_span.c
    ../../components/esp32_mquickjs/src/core/esp32_mquickjs_byte_span.c
)
target_include_directories(test_byte_span PRIVATE ../../components/esp32_mquickjs/internal)
target_include_directories(test_byte_span PRIVATE ../../components/esp32_mquickjs/vendor/mquickjs)
add_test(NAME test_byte_span COMMAND test_byte_span)

add_executable(test_bitmap_image
    test_bitmap_image.c
    ../../components/esp32_mquickjs/src/modules/bitmap/esp32_mquickjs_bitmap_image_core.c
)
target_include_directories(test_bitmap_image PRIVATE ../../components/esp32_mquickjs/internal)
add_test(NAME test_bitmap_image COMMAND test_bitmap_image)

add_executable(test_peripheral_lease
    test_peripheral_lease.c
    ../../components/esp32_mquickjs/src/core/esp32_mquickjs_peripheral_lease.c
)
target_include_directories(test_peripheral_lease PRIVATE ../../components/esp32_mquickjs/internal)
add_test(NAME test_peripheral_lease COMMAND test_peripheral_lease)

add_executable(test_line_framer
    test_line_framer.c
    ../../components/esp32_mquickjs/src/core/esp32_mquickjs_line_framer.c
)
target_include_directories(test_line_framer PRIVATE ../../components/esp32_mquickjs/internal)
add_test(NAME test_line_framer COMMAND test_line_framer)

add_executable(test_rpc_wire
    test_rpc_wire.c
    ../../components/esp32_mquickjs/src/core/esp32_mquickjs_rpc_wire.c
)
target_include_directories(test_rpc_wire PRIVATE ../../components/esp32_mquickjs/include)
target_compile_definitions(test_rpc_wire PRIVATE
    RPC_GOLDEN_VECTOR_PATH="${CMAKE_CURRENT_SOURCE_DIR}/rpc-v1-golden.txt"
)
add_test(NAME test_rpc_wire COMMAND test_rpc_wire)

add_executable(test_log_ring
    test_log_ring.c
    ../../components/esp32_mquickjs/src/core/esp32_mquickjs_log_ring.c
)
target_include_directories(test_log_ring PRIVATE ../../components/esp32_mquickjs/internal)
add_test(NAME test_log_ring COMMAND test_log_ring)

add_executable(test_event_queue_drain
    test_event_queue_drain.c
    ../../components/esp32_mquickjs/src/core/esp32_mquickjs_event_queue_drain.c
)
target_include_directories(test_event_queue_drain PRIVATE ../../components/esp32_mquickjs/internal)
add_test(NAME test_event_queue_drain COMMAND test_event_queue_drain)

add_executable(test_event_queue_resources
    test_event_queue_resources.c
    ../../components/esp32_mquickjs/src/core/esp32_mquickjs_event_queue_resources.c
)
target_include_directories(test_event_queue_resources PRIVATE ../../components/esp32_mquickjs/internal)
add_test(NAME test_event_queue_resources COMMAND test_event_queue_resources)

add_executable(test_future_timeout
    test_future_timeout.c
    ../../components/esp32_mquickjs/src/core/esp32_mquickjs_future_timeout.c
)
target_include_directories(test_future_timeout PRIVATE ../../components/esp32_mquickjs/internal)
add_test(NAME test_future_timeout COMMAND test_future_timeout)

add_executable(test_timer_resource
    test_timer_resource.c
    ../../components/esp32_mquickjs/src/core/esp32_mquickjs_timer_resource.c
)
target_include_directories(test_timer_resource PRIVATE ../../components/esp32_mquickjs/internal)
add_test(NAME test_timer_resource COMMAND test_timer_resource)

add_executable(test_future_worker_pool
    test_future_worker_pool.c
    ../../components/esp32_mquickjs/src/core/esp32_mquickjs_future_worker_pool.c
)
target_include_directories(test_future_worker_pool PRIVATE ../../components/esp32_mquickjs/internal)
add_test(NAME test_future_worker_pool COMMAND test_future_worker_pool)

add_executable(test_future_scheduler
    test_future_scheduler.c
    ../../components/esp32_mquickjs/src/core/esp32_mquickjs_future_scheduler.c
)
target_include_directories(test_future_scheduler PRIVATE ../../components/esp32_mquickjs/internal)
add_test(NAME test_future_scheduler COMMAND test_future_scheduler)

add_executable(test_future_runtime_resources
    test_future_runtime_resources.c
    ../../components/esp32_mquickjs/src/core/esp32_mquickjs_future_runtime_resources.c
)
target_include_directories(test_future_runtime_resources PRIVATE ../../components/esp32_mquickjs/internal)
add_test(NAME test_future_runtime_resources COMMAND test_future_runtime_resources)

add_executable(test_usb_serial_future_lifecycle
    test_usb_serial_future_lifecycle.c
    ../../components/esp32_mquickjs/src/modules/usb_serial/esp32_mquickjs_usb_serial_future_lifecycle.c
)
target_include_directories(test_usb_serial_future_lifecycle PRIVATE ../../components/esp32_mquickjs/internal)
add_test(NAME test_usb_serial_future_lifecycle COMMAND test_usb_serial_future_lifecycle)

add_executable(test_http_operation_resources
    test_http_operation_resources.c
    ../../components/esp32_mquickjs/src/modules/http/esp32_mquickjs_http_operation_resources.c
)
target_include_directories(test_http_operation_resources PRIVATE ../../components/esp32_mquickjs/internal)
add_test(NAME test_http_operation_resources COMMAND test_http_operation_resources)

add_executable(test_http_client_resources
    test_http_client_resources.c
    ../../components/esp32_mquickjs/src/modules/http/esp32_mquickjs_http_client_resources.c
)
target_include_directories(test_http_client_resources PRIVATE ../../components/esp32_mquickjs/internal)
add_test(NAME test_http_client_resources COMMAND test_http_client_resources)

add_executable(test_http_server_resources
    test_http_server_resources.c
    ../../components/esp32_mquickjs/src/modules/http/esp32_mquickjs_http_server_resources.c
)
target_include_directories(test_http_server_resources PRIVATE ../../components/esp32_mquickjs/internal)
add_test(NAME test_http_server_resources COMMAND test_http_server_resources)

add_executable(test_websocket_client_resources
    test_websocket_client_resources.c
    ../../components/esp32_mquickjs/src/modules/websocket/esp32_mquickjs_websocket_client_resources.c
)
target_include_directories(test_websocket_client_resources PRIVATE ../../components/esp32_mquickjs/internal)
add_test(NAME test_websocket_client_resources COMMAND test_websocket_client_resources)

add_executable(test_rmt_symbol_buffer_resources
    test_rmt_symbol_buffer_resources.c
    ../../components/esp32_mquickjs/src/modules/rmt/esp32_mquickjs_rmt_symbol_buffer_resources.c
)
target_include_directories(test_rmt_symbol_buffer_resources PRIVATE ../../components/esp32_mquickjs/internal)
add_test(NAME test_rmt_symbol_buffer_resources COMMAND test_rmt_symbol_buffer_resources)

add_executable(test_rmt_channel_resources
    test_rmt_channel_resources.c
    ../../components/esp32_mquickjs/src/modules/rmt/esp32_mquickjs_rmt_channel_resources.c
)
target_include_directories(test_rmt_channel_resources PRIVATE ../../components/esp32_mquickjs/internal)
add_test(NAME test_rmt_channel_resources COMMAND test_rmt_channel_resources)

add_executable(test_wifi_runtime_resources
    test_wifi_runtime_resources.c
    ../../components/esp32_mquickjs/src/modules/wifi/esp32_mquickjs_wifi_runtime_resources.c
)
target_include_directories(test_wifi_runtime_resources PRIVATE ../../components/esp32_mquickjs/internal)
add_test(NAME test_wifi_runtime_resources COMMAND test_wifi_runtime_resources)

add_executable(test_i2s_timer_resources
    test_i2s_timer_resources.c
    ../../components/esp32_mquickjs/src/modules/i2s/esp32_mquickjs_i2s_timer_resources.c
)
target_include_directories(test_i2s_timer_resources PRIVATE ../../components/esp32_mquickjs/internal)
add_test(NAME test_i2s_timer_resources COMMAND test_i2s_timer_resources)

add_executable(test_i2s_channel_resources
    test_i2s_channel_resources.c
    ../../components/esp32_mquickjs/src/modules/i2s/esp32_mquickjs_i2s_channel_resources.c
)
target_include_directories(test_i2s_channel_resources PRIVATE ../../components/esp32_mquickjs/internal)
add_test(NAME test_i2s_channel_resources COMMAND test_i2s_channel_resources)

add_executable(test_adc_unit_resources
    test_adc_unit_resources.c
    ../../components/esp32_mquickjs/src/modules/adc/esp32_mquickjs_adc_unit_resources.c
)
target_include_directories(test_adc_unit_resources PRIVATE ../../components/esp32_mquickjs/internal)
add_test(NAME test_adc_unit_resources COMMAND test_adc_unit_resources)

add_executable(test_ledc_resources
    test_ledc_resources.c
    ../../components/esp32_mquickjs/src/modules/ledc/esp32_mquickjs_ledc_resources.c
)
target_include_directories(test_ledc_resources PRIVATE ../../components/esp32_mquickjs/internal)
add_test(NAME test_ledc_resources COMMAND test_ledc_resources)

add_executable(test_camera_driver_resources
    test_camera_driver_resources.c
    ../../components/esp32_mquickjs/src/modules/camera/esp32_mquickjs_camera_driver_resources.c
)
target_include_directories(test_camera_driver_resources PRIVATE ../../components/esp32_mquickjs/internal)
add_test(NAME test_camera_driver_resources COMMAND test_camera_driver_resources)

add_executable(test_spi_native_resources
    test_spi_native_resources.c
    ../../components/esp32_mquickjs/src/modules/spi/esp32_mquickjs_spi_native_resources.c
)
target_include_directories(test_spi_native_resources PRIVATE ../../components/esp32_mquickjs/internal)
add_test(NAME test_spi_native_resources COMMAND test_spi_native_resources)

add_executable(test_i2c_bus_resources
    test_i2c_bus_resources.c
    ../../components/esp32_mquickjs/src/modules/i2c/esp32_mquickjs_i2c_bus_resources.c
)
target_include_directories(test_i2c_bus_resources PRIVATE ../../components/esp32_mquickjs/internal)
add_test(NAME test_i2c_bus_resources COMMAND test_i2c_bus_resources)

add_executable(test_uart_port_resources
    test_uart_port_resources.c
    ../../components/esp32_mquickjs/src/modules/uart/esp32_mquickjs_uart_port_resources.c
)
target_include_directories(test_uart_port_resources PRIVATE ../../components/esp32_mquickjs/internal)
add_test(NAME test_uart_port_resources COMMAND test_uart_port_resources)

add_executable(test_ble_runtime_resources
    test_ble_runtime_resources.c
    ../../components/esp32_mquickjs/src/modules/ble/esp32_mquickjs_ble_runtime_resources.c
)
target_include_directories(test_ble_runtime_resources PRIVATE ../../components/esp32_mquickjs/internal)
add_test(NAME test_ble_runtime_resources COMMAND test_ble_runtime_resources)

add_executable(test_memory_dma_accounting
    test_memory_dma_accounting.c
    ../../components/esp32_mquickjs/src/core/esp32_mquickjs_memory_dma_accounting.c
)
target_include_directories(test_memory_dma_accounting PRIVATE ../../components/esp32_mquickjs/internal)
add_test(NAME test_memory_dma_accounting COMMAND test_memory_dma_accounting)

add_executable(test_dma_transfer
    test_dma_transfer.c
    ../../components/esp32_mquickjs/src/core/esp32_mquickjs_dma_transfer.c
)
target_include_directories(test_dma_transfer PRIVATE ../../components/esp32_mquickjs/internal)
add_test(NAME test_dma_transfer COMMAND test_dma_transfer)

add_executable(test_wireless_core
    test_wireless_core.c
    ../../components/esp32_mquickjs/src/core/esp32_mquickjs_wireless_core.c
)
target_include_directories(test_wireless_core PRIVATE ../../components/esp32_mquickjs/internal)
add_test(NAME test_wireless_core COMMAND test_wireless_core)
