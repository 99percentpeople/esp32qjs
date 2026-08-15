# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.22)

# Runtime/resource tuning options live in component Kconfig files.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/components/esp32qjs_runtime/Kconfig.projbuild"
    "${CMAKE_SOURCE_DIR}/components/esp32_mquickjs/Kconfig.projbuild"
    "${CMAKE_SOURCE_DIR}/components/esp32qjs_interactive/Kconfig.projbuild")

file(GLOB_RECURSE _esp32qjs_profile_config_files CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/configs/mcus/*/.env"
    "${CMAKE_SOURCE_DIR}/configs/mcus/*/sdkconfig.defaults"
    "${CMAKE_SOURCE_DIR}/apps/*/app.env"
    "${CMAKE_SOURCE_DIR}/apps/*/sdkconfig.defaults"
    "${CMAKE_SOURCE_DIR}/apps/*/partitions/*.csv")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_esp32qjs_profile_config_files})

set(ESP32QJS_MCU "esp32s3" CACHE STRING "MCU capability profile under configs/mcus/")
set(ESP32QJS_APP "minimal" CACHE STRING "Selected application identifier")
set(ESP32QJS_APP_PROFILE_DIR "${CMAKE_SOURCE_DIR}/apps/${ESP32QJS_APP}" CACHE PATH
    "Selected bundled or external application profile directory")
get_filename_component(ESP32QJS_APP_PROFILE_DIR "${ESP32QJS_APP_PROFILE_DIR}"
    ABSOLUTE BASE_DIR "${CMAKE_SOURCE_DIR}")
set(ESP32QJS_MCU_SDKCONFIG_DEFAULTS "" CACHE FILEPATH
    "Optional MCU sdkconfig defaults override")
set(ESP32QJS_APP_SDKCONFIG_DEFAULTS "" CACHE FILEPATH
    "Optional application sdkconfig defaults override")
set(ESP32QJS_PARTITION_TABLE "" CACHE FILEPATH
    "Optional application partition table override")
set(ESP32QJS_PROFILE_CONSTANTS_FILE "" CACHE FILEPATH
    "Generated immutable hardware-profile constants include")
set(ESP32QJS_SHARED_FLASH_DATA_DIR "${CMAKE_SOURCE_DIR}/shared/flash_data" CACHE PATH
    "Shared files copied into every application LittleFS image")
set(ESP32QJS_APP_FLASH_DATA_DIR "${ESP32QJS_APP_PROFILE_DIR}/flash_data" CACHE PATH
    "Selected application files overlaid onto the LittleFS image")
set(ESP32QJS_FLASH_DATA_DIR "" CACHE PATH
    "Optional LittleFS source override (used by tests and compatibility workflows)")
set(ESP32QJS_FLASH_DATA_INCLUDE_SHARED OFF CACHE BOOL
    "Overlay a LittleFS override onto shared framework resources")

file(GLOB_RECURSE _esp32qjs_selected_app_config_files CONFIGURE_DEPENDS
    "${ESP32QJS_APP_PROFILE_DIR}/app.env"
    "${ESP32QJS_APP_PROFILE_DIR}/sdkconfig.defaults"
    "${ESP32QJS_APP_PROFILE_DIR}/partitions/*.csv")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    ${_esp32qjs_selected_app_config_files})

set(_esp32qjs_flash_data_files)
foreach(_esp32qjs_flash_data_dir
        "${ESP32QJS_SHARED_FLASH_DATA_DIR}"
        "${ESP32QJS_APP_FLASH_DATA_DIR}"
        "${ESP32QJS_FLASH_DATA_DIR}")
    if(IS_DIRECTORY "${_esp32qjs_flash_data_dir}")
        file(GLOB_RECURSE _esp32qjs_flash_data_dir_files CONFIGURE_DEPENDS
            "${_esp32qjs_flash_data_dir}/*")
        list(APPEND _esp32qjs_flash_data_files ${_esp32qjs_flash_data_dir_files})
    endif()
endforeach()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_esp32qjs_flash_data_files})

set(_esp32qjs_mcu_defaults "${ESP32QJS_MCU_SDKCONFIG_DEFAULTS}")
if(_esp32qjs_mcu_defaults STREQUAL "")
    set(_esp32qjs_mcu_defaults
        "${CMAKE_SOURCE_DIR}/configs/mcus/${ESP32QJS_MCU}/sdkconfig.defaults")
endif()
set(_esp32qjs_app_defaults "${ESP32QJS_APP_SDKCONFIG_DEFAULTS}")
if(_esp32qjs_app_defaults STREQUAL "")
    set(_esp32qjs_app_defaults
        "${ESP32QJS_APP_PROFILE_DIR}/sdkconfig.defaults")
endif()
if(NOT DEFINED SDKCONFIG_DEFAULTS OR SDKCONFIG_DEFAULTS STREQUAL "")
    foreach(_esp32qjs_defaults "${_esp32qjs_mcu_defaults}" "${_esp32qjs_app_defaults}")
        if(EXISTS "${_esp32qjs_defaults}")
            list(APPEND SDKCONFIG_DEFAULTS "${_esp32qjs_defaults}")
        endif()
    endforeach()
endif()

set(_esp32qjs_partition_table "${ESP32QJS_PARTITION_TABLE}")
if(_esp32qjs_partition_table STREQUAL "")
    message(FATAL_ERROR "ESP32QJS_PARTITION_TABLE must be generated for the selected flash size")
endif()
get_filename_component(_esp32qjs_partition_table "${_esp32qjs_partition_table}"
    ABSOLUTE BASE_DIR "${CMAKE_SOURCE_DIR}")
if(NOT EXISTS "${_esp32qjs_partition_table}")
    message(FATAL_ERROR
        "ESP32QJS partition table does not exist: ${_esp32qjs_partition_table}")
endif()
file(TO_CMAKE_PATH "${_esp32qjs_partition_table}" _esp32qjs_partition_table_kconfig)
set(_esp32qjs_partition_defaults
    "${CMAKE_BINARY_DIR}/esp32qjs_partition.defaults")
set(_esp32qjs_partition_defaults_content
    "CONFIG_PARTITION_TABLE_CUSTOM=y\nCONFIG_PARTITION_TABLE_CUSTOM_FILENAME=\"${_esp32qjs_partition_table_kconfig}\"\n")
set(_esp32qjs_previous_partition_defaults "")
if(EXISTS "${_esp32qjs_partition_defaults}")
    file(READ "${_esp32qjs_partition_defaults}" _esp32qjs_previous_partition_defaults)
endif()
if(NOT _esp32qjs_previous_partition_defaults STREQUAL _esp32qjs_partition_defaults_content)
    file(WRITE "${_esp32qjs_partition_defaults}"
        "${_esp32qjs_partition_defaults_content}")
endif()
list(APPEND SDKCONFIG_DEFAULTS "${_esp32qjs_partition_defaults}")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(DEPENDENCIES_LOCK "dependencies.lock.${IDF_TARGET}")
project(esp32qjs)
