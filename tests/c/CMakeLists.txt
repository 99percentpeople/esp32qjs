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
