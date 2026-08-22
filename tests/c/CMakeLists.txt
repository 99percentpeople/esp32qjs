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
