# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly.
cmake_minimum_required(VERSION 3.22)

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/components/esp32qjs_runtime/Kconfig.projbuild"
    "${CMAKE_SOURCE_DIR}/components/esp32_mquickjs/Kconfig.projbuild"
    "${CMAKE_SOURCE_DIR}/components/esp32qjs_interactive/Kconfig.projbuild"
    "${CMAKE_SOURCE_DIR}/components/esp32_mquickjs/runtime-features.json")

set(ESP32QJS_MCU "esp32s3" CACHE STRING
    "MCU capability profile under configs/mcus/")
set(ESP32QJS_BUILD_CONTEXT_DIR "" CACHE PATH
    "Resolved immutable Build Context directory")
set(ESP32QJS_MCU_SDKCONFIG_DEFAULTS "" CACHE FILEPATH
    "Optional MCU sdkconfig defaults override")

if(ESP32QJS_BUILD_CONTEXT_DIR STREQUAL "")
    message(FATAL_ERROR
        "ESP32QJS_BUILD_CONTEXT_DIR is required; resolve a Board in the Hub or use a firmware test fixture")
endif()
get_filename_component(ESP32QJS_BUILD_CONTEXT_DIR
    "${ESP32QJS_BUILD_CONTEXT_DIR}" ABSOLUTE BASE_DIR "${CMAKE_SOURCE_DIR}")

foreach(_esp32qjs_required_context_entry
        manifest.json sdkconfig.defaults partitions.csv profile-constants.inc
        precompile.json flash_data)
    if(NOT EXISTS
       "${ESP32QJS_BUILD_CONTEXT_DIR}/${_esp32qjs_required_context_entry}")
        message(FATAL_ERROR
            "Build Context is missing ${_esp32qjs_required_context_entry}: ${ESP32QJS_BUILD_CONTEXT_DIR}")
    endif()
endforeach()

file(GLOB_RECURSE _esp32qjs_context_files CONFIGURE_DEPENDS
    "${ESP32QJS_BUILD_CONTEXT_DIR}/*")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    ${_esp32qjs_context_files})
set(ESP32QJS_PROFILE_CONSTANTS_FILE
    "${ESP32QJS_BUILD_CONTEXT_DIR}/profile-constants.inc")

set(_esp32qjs_mcu_defaults "${ESP32QJS_MCU_SDKCONFIG_DEFAULTS}")
if(_esp32qjs_mcu_defaults STREQUAL "")
    set(_esp32qjs_mcu_defaults
        "${CMAKE_SOURCE_DIR}/configs/mcus/${ESP32QJS_MCU}/sdkconfig.defaults")
endif()
if(NOT DEFINED SDKCONFIG_DEFAULTS OR SDKCONFIG_DEFAULTS STREQUAL "")
    set(SDKCONFIG_DEFAULTS
        "${_esp32qjs_mcu_defaults}"
        "${ESP32QJS_BUILD_CONTEXT_DIR}/sdkconfig.defaults")
endif()

file(TO_CMAKE_PATH
    "${ESP32QJS_BUILD_CONTEXT_DIR}/partitions.csv"
    _esp32qjs_partition_table_kconfig)
set(_esp32qjs_partition_defaults
    "${CMAKE_BINARY_DIR}/esp32qjs_partition.defaults")
file(WRITE "${_esp32qjs_partition_defaults}"
    "CONFIG_PARTITION_TABLE_CUSTOM=y\nCONFIG_PARTITION_TABLE_CUSTOM_FILENAME=\"${_esp32qjs_partition_table_kconfig}\"\n")
list(APPEND SDKCONFIG_DEFAULTS "${_esp32qjs_partition_defaults}")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(DEPENDENCIES_LOCK "dependencies.lock.${IDF_TARGET}")
project(esp32qjs)

include("${CMAKE_SOURCE_DIR}/scripts/patch_esp32_camera_2_1_7.cmake")
