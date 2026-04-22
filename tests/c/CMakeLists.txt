cmake_minimum_required(VERSION 3.16)

project(esp32qjs_host_tests C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

enable_testing()

add_executable(test_repl_logic
    test_repl_logic.c
    ../../main/repl_line.c
)
target_include_directories(test_repl_logic PRIVATE ../../main)
add_test(NAME test_repl_logic COMMAND test_repl_logic)

add_executable(test_fs_path
    test_fs_path.c
    ../../components/esp32_mquickjs/src/esp32_mquickjs_fs_path.c
)
target_include_directories(test_fs_path PRIVATE ../../components/esp32_mquickjs/src)
add_test(NAME test_fs_path COMMAND test_fs_path)
