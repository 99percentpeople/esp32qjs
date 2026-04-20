# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.22)

# App/resource tuning options live in main/Kconfig.projbuild and
# components/esp32_mquickjs/Kconfig.projbuild.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/main/Kconfig.projbuild"
    "${CMAKE_SOURCE_DIR}/components/esp32_mquickjs/Kconfig.projbuild")

file(GLOB_RECURSE _esp32qjs_board_config_files CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/configs/boards/*/.env"
    "${CMAKE_SOURCE_DIR}/configs/boards/*/sdkconfig.defaults")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_esp32qjs_board_config_files})

set(ESP32QJS_BOARD "xiao_esp32s3" CACHE STRING "Default board profile under configs/boards/")

if(NOT DEFINED SDKCONFIG_DEFAULTS OR SDKCONFIG_DEFAULTS STREQUAL "")
    set(_esp32qjs_board_defaults
        "${CMAKE_SOURCE_DIR}/configs/boards/${ESP32QJS_BOARD}/sdkconfig.defaults")
    if(EXISTS "${_esp32qjs_board_defaults}")
        list(APPEND SDKCONFIG_DEFAULTS "${_esp32qjs_board_defaults}")
    endif()
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32qjs)
