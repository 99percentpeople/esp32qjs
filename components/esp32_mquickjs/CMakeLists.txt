set(MQUICKJS_DIR "${CMAKE_CURRENT_LIST_DIR}/vendor/mquickjs")
set(MQUICKJS_GENERATED_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/generated")
set(MQUICKJS_GENERATED_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(MQUICKJS_HOST_BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/host_tools")
set(MQUICKJS_HOST_TOOL "${MQUICKJS_HOST_BUILD_DIR}/mqjs_stdlib")
set(MQUICKJS_RUN_TOOL_TO_FILE_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/cmake/run_tool_to_file.cmake")
set(MQUICKJS_PATCH_BUILD_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/cmake/patch_mquickjs_build.cmake")
set(MQUICKJS_STDLIB_SOURCE "${CMAKE_CURRENT_LIST_DIR}/src/core/mqjs_stdlib_esp32.c")
set(MQUICKJS_HOST_BUILD_SOURCE "${MQUICKJS_HOST_BUILD_DIR}/mquickjs_build.host.c")
set(ESP32_MQUICKJS_CONFIG_FEATURE_SYMBOLS
    CONFIG_ESP32_MQUICKJS_FEATURE_FS
    CONFIG_ESP32_MQUICKJS_FEATURE_GPIO
    CONFIG_ESP32_MQUICKJS_FEATURE_LEDC
    CONFIG_ESP32_MQUICKJS_FEATURE_ADC
    CONFIG_ESP32_MQUICKJS_FEATURE_DAC
    CONFIG_ESP32_MQUICKJS_FEATURE_I2C
    CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
    CONFIG_ESP32_MQUICKJS_FEATURE_HTTP
    CONFIG_ESP32_MQUICKJS_FEATURE_HTTP_SERVER
)

macro(esp32_mquickjs_load_feature_config)
    if(CMAKE_BUILD_EARLY_EXPANSION)
        set(_esp32_mquickjs_sdkconfig_cmake "${CMAKE_BINARY_DIR}/config/sdkconfig.cmake")
        if(EXISTS "${_esp32_mquickjs_sdkconfig_cmake}")
            include("${_esp32_mquickjs_sdkconfig_cmake}")
        else()
            set(_esp32_mquickjs_config_files "")
            idf_build_get_property(_esp32_mquickjs_sdkconfig_defaults SDKCONFIG_DEFAULTS)
            if(_esp32_mquickjs_sdkconfig_defaults)
                list(APPEND _esp32_mquickjs_config_files ${_esp32_mquickjs_sdkconfig_defaults})
            endif()

            idf_build_get_property(_esp32_mquickjs_sdkconfig SDKCONFIG)
            if(_esp32_mquickjs_sdkconfig)
                list(APPEND _esp32_mquickjs_config_files "${_esp32_mquickjs_sdkconfig}")
            endif()

            foreach(_esp32_mquickjs_config_file ${_esp32_mquickjs_config_files})
                if(NOT EXISTS "${_esp32_mquickjs_config_file}")
                    continue()
                endif()

                file(
                    STRINGS
                    "${_esp32_mquickjs_config_file}"
                    _esp32_mquickjs_feature_lines
                    REGEX "^(CONFIG_ESP32_MQUICKJS_FEATURE_[A-Z0-9_]+=.*|# CONFIG_ESP32_MQUICKJS_FEATURE_[A-Z0-9_]+ is not set)$"
                )

                foreach(_esp32_mquickjs_line ${_esp32_mquickjs_feature_lines})
                    if(_esp32_mquickjs_line MATCHES "^CONFIG_ESP32_MQUICKJS_FEATURE_[A-Z0-9_]+=")
                        string(REGEX REPLACE "^([^=]+)=.*$" "\\1" _esp32_mquickjs_key "${_esp32_mquickjs_line}")
                        string(REGEX REPLACE "^[^=]+=([^#]+).*$" "\\1" _esp32_mquickjs_value "${_esp32_mquickjs_line}")
                        string(STRIP "${_esp32_mquickjs_value}" _esp32_mquickjs_value)
                        set(${_esp32_mquickjs_key} "${_esp32_mquickjs_value}")
                    elseif(_esp32_mquickjs_line MATCHES "^# CONFIG_ESP32_MQUICKJS_FEATURE_[A-Z0-9_]+ is not set$")
                        string(REGEX REPLACE "^# (CONFIG_ESP32_MQUICKJS_FEATURE_[A-Z0-9_]+) is not set$" "\\1" _esp32_mquickjs_key "${_esp32_mquickjs_line}")
                        set(${_esp32_mquickjs_key} "n")
                    endif()
                endforeach()
            endforeach()
        endif()
    endif()
endmacro()

esp32_mquickjs_load_feature_config()

if(DEFINED ENV{HOSTCC} AND NOT "$ENV{HOSTCC}" STREQUAL "")
    set(MQUICKJS_HOST_CC "$ENV{HOSTCC}")
else()
    find_program(MQUICKJS_HOST_CC NAMES cc gcc clang REQUIRED NO_CACHE)
endif()

set(MQUICKJS_HOST_CFLAGS
    -Wall
    -g
    -MMD
    -D_GNU_SOURCE
    -I${MQUICKJS_DIR}
    -fno-math-errno
    -fno-trapping-math
    -O2
)

set(MQUICKJS_HOST_LDFLAGS
    -g
)

function(esp32_mquickjs_append_host_feature_define config_symbol)
    if(DEFINED ${config_symbol} AND ${config_symbol})
        list(APPEND MQUICKJS_HOST_CFLAGS "-D${config_symbol}=1")
    else()
        list(APPEND MQUICKJS_HOST_CFLAGS "-D${config_symbol}=0")
    endif()
    set(MQUICKJS_HOST_CFLAGS "${MQUICKJS_HOST_CFLAGS}" PARENT_SCOPE)
endfunction()

function(esp32_mquickjs_append_feature_component config_symbol)
    if(NOT (DEFINED ${config_symbol} AND ${config_symbol}))
        return()
    endif()

    set(_esp32_mquickjs_mode SRCS)
    set(_esp32_mquickjs_srcs)
    set(_esp32_mquickjs_priv_requires)

    foreach(_esp32_mquickjs_item ${ARGN})
        if(_esp32_mquickjs_item STREQUAL "PRIV_REQUIRES")
            set(_esp32_mquickjs_mode PRIV_REQUIRES)
        elseif(_esp32_mquickjs_mode STREQUAL "SRCS")
            list(APPEND _esp32_mquickjs_srcs "${_esp32_mquickjs_item}")
        else()
            list(APPEND _esp32_mquickjs_priv_requires "${_esp32_mquickjs_item}")
        endif()
    endforeach()

    list(APPEND ESP32_MQUICKJS_SRCS ${_esp32_mquickjs_srcs})
    list(APPEND ESP32_MQUICKJS_PRIV_REQUIRES ${_esp32_mquickjs_priv_requires})
    set(ESP32_MQUICKJS_SRCS "${ESP32_MQUICKJS_SRCS}" PARENT_SCOPE)
    set(ESP32_MQUICKJS_PRIV_REQUIRES "${ESP32_MQUICKJS_PRIV_REQUIRES}" PARENT_SCOPE)
endfunction()

foreach(esp32_mquickjs_feature_config ${ESP32_MQUICKJS_CONFIG_FEATURE_SYMBOLS})
    esp32_mquickjs_append_host_feature_define(${esp32_mquickjs_feature_config})
endforeach()

set(MQUICKJS_ATOM_HEADER "${MQUICKJS_GENERATED_BINARY_DIR}/mquickjs_atom.h")
set(MQUICKJS_STDLIB_HEADER "${MQUICKJS_GENERATED_BINARY_DIR}/mqjs_stdlib.h")

file(MAKE_DIRECTORY "${MQUICKJS_GENERATED_BINARY_DIR}")

if(NOT CMAKE_SCRIPT_MODE_FILE)
    add_custom_command(
        OUTPUT "${MQUICKJS_HOST_TOOL}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${MQUICKJS_HOST_BUILD_DIR}"
        COMMAND ${CMAKE_COMMAND}
                -DINPUT_FILE=${MQUICKJS_DIR}/mquickjs_build.c
                -DOUTPUT_FILE=${MQUICKJS_HOST_BUILD_SOURCE}
                -P "${MQUICKJS_PATCH_BUILD_SCRIPT}"
        COMMAND ${MQUICKJS_HOST_CC}
                ${MQUICKJS_HOST_CFLAGS}
                -c
                -o "${MQUICKJS_HOST_BUILD_DIR}/mqjs_stdlib.host.o"
                "${MQUICKJS_STDLIB_SOURCE}"
        COMMAND ${MQUICKJS_HOST_CC}
                ${MQUICKJS_HOST_CFLAGS}
                -c
                -o "${MQUICKJS_HOST_BUILD_DIR}/mquickjs_build.host.o"
                "${MQUICKJS_HOST_BUILD_SOURCE}"
        COMMAND ${MQUICKJS_HOST_CC}
                ${MQUICKJS_HOST_LDFLAGS}
                -o "${MQUICKJS_HOST_TOOL}"
                "${MQUICKJS_HOST_BUILD_DIR}/mqjs_stdlib.host.o"
                "${MQUICKJS_HOST_BUILD_DIR}/mquickjs_build.host.o"
        DEPENDS
            "${MQUICKJS_PATCH_BUILD_SCRIPT}"
            "${MQUICKJS_STDLIB_SOURCE}"
            "${MQUICKJS_DIR}/mqjs_stdlib.c"
            "${MQUICKJS_DIR}/mquickjs_build.c"
            "${MQUICKJS_DIR}/mquickjs.h"
            "${MQUICKJS_DIR}/cutils.h"
        VERBATIM
    )

    add_custom_command(
        OUTPUT
            "${MQUICKJS_ATOM_HEADER}"
            "${MQUICKJS_STDLIB_HEADER}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${MQUICKJS_GENERATED_BINARY_DIR}"
        COMMAND ${CMAKE_COMMAND}
                -DOUTPUT_FILE=${MQUICKJS_ATOM_HEADER}
                -DTOOL=${MQUICKJS_HOST_TOOL}
                -DARG0=-a
                -DARG1=-m32
                -P "${MQUICKJS_RUN_TOOL_TO_FILE_SCRIPT}"
        COMMAND ${CMAKE_COMMAND}
                -DOUTPUT_FILE=${MQUICKJS_STDLIB_HEADER}
                -DTOOL=${MQUICKJS_HOST_TOOL}
                -DARG0=-m32
                -P "${MQUICKJS_RUN_TOOL_TO_FILE_SCRIPT}"
        DEPENDS
            "${MQUICKJS_HOST_TOOL}"
            "${MQUICKJS_RUN_TOOL_TO_FILE_SCRIPT}"
        VERBATIM
    )

    add_custom_target(mquickjs_generated_headers
        DEPENDS
            "${MQUICKJS_ATOM_HEADER}"
            "${MQUICKJS_STDLIB_HEADER}"
    )

    add_custom_command(
        OUTPUT
            "${MQUICKJS_GENERATED_SOURCE_DIR}/mquickjs_atom.h"
            "${MQUICKJS_GENERATED_SOURCE_DIR}/mqjs_stdlib.h"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${MQUICKJS_GENERATED_SOURCE_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MQUICKJS_ATOM_HEADER}"
                "${MQUICKJS_GENERATED_SOURCE_DIR}/mquickjs_atom.h"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MQUICKJS_STDLIB_HEADER}"
                "${MQUICKJS_GENERATED_SOURCE_DIR}/mqjs_stdlib.h"
        DEPENDS mquickjs_generated_headers
        VERBATIM
    )

    add_custom_target(update_mquickjs_headers
        DEPENDS
            "${MQUICKJS_GENERATED_SOURCE_DIR}/mquickjs_atom.h"
            "${MQUICKJS_GENERATED_SOURCE_DIR}/mqjs_stdlib.h"
    )
endif()

set(ESP32_MQUICKJS_SRCS
    "src/core/esp32_mquickjs.c"
    "src/core/esp32_mquickjs_esp32.c"
    "src/core/esp32_mquickjs_stream.c"
    "src/core/esp32_mquickjs_request_response.c"
    "src/core/js_stdlib.c"
    "${MQUICKJS_DIR}/cutils.c"
    "${MQUICKJS_DIR}/dtoa.c"
    "${MQUICKJS_DIR}/libm.c"
    "${MQUICKJS_DIR}/mquickjs.c"
)

set(ESP32_MQUICKJS_PUBLIC_REQUIRES
    esp_common
)

set(ESP32_MQUICKJS_PRIV_REQUIRES
    esp_timer
    spi_flash
    esp_psram
)

esp32_mquickjs_append_feature_component(
    CONFIG_ESP32_MQUICKJS_FEATURE_FS
    "src/modules/fs/esp32_mquickjs_fs.c"
    "src/modules/fs/esp32_mquickjs_fs_path.c"
    PRIV_REQUIRES
        joltwallet__littlefs
)

esp32_mquickjs_append_feature_component(
    CONFIG_ESP32_MQUICKJS_FEATURE_GPIO
    "src/modules/gpio/esp32_mquickjs_gpio.c"
    PRIV_REQUIRES
        esp_driver_gpio
)

esp32_mquickjs_append_feature_component(
    CONFIG_ESP32_MQUICKJS_FEATURE_LEDC
    "src/modules/ledc/esp32_mquickjs_ledc.c"
    PRIV_REQUIRES
        esp_driver_gpio
        esp_driver_ledc
)

esp32_mquickjs_append_feature_component(
    CONFIG_ESP32_MQUICKJS_FEATURE_ADC
    "src/modules/adc/esp32_mquickjs_adc.c"
    PRIV_REQUIRES
        esp_adc
)

esp32_mquickjs_append_feature_component(
    CONFIG_ESP32_MQUICKJS_FEATURE_DAC
    "src/modules/dac/esp32_mquickjs_dac.c"
    PRIV_REQUIRES
        esp_driver_dac
)

esp32_mquickjs_append_feature_component(
    CONFIG_ESP32_MQUICKJS_FEATURE_I2C
    "src/modules/i2c/esp32_mquickjs_i2c.c"
    PRIV_REQUIRES
        esp_driver_gpio
        esp_driver_i2c
)

esp32_mquickjs_append_feature_component(
    CONFIG_ESP32_MQUICKJS_FEATURE_HTTP
    "src/modules/http/esp32_mquickjs_http.c"
    PRIV_REQUIRES
        esp_http_client
        mbedtls
)

esp32_mquickjs_append_feature_component(
    CONFIG_ESP32_MQUICKJS_FEATURE_HTTP_SERVER
    "src/modules/http/esp32_mquickjs_http_server.c"
    PRIV_REQUIRES
        esp_http_server
        esp_netif
)

esp32_mquickjs_append_feature_component(
    CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
    "src/modules/wifi/esp32_mquickjs_wifi.c"
    PRIV_REQUIRES
        esp_event
        esp_netif
        esp_wifi
        nvs_flash
)

list(REMOVE_DUPLICATES ESP32_MQUICKJS_PRIV_REQUIRES)

idf_component_register(
    SRCS ${ESP32_MQUICKJS_SRCS}
    INCLUDE_DIRS
        "include"
        "${MQUICKJS_DIR}"
    PRIV_INCLUDE_DIRS
        "internal"
        "${MQUICKJS_GENERATED_SOURCE_DIR}"
        "${MQUICKJS_GENERATED_BINARY_DIR}"
        "${MQUICKJS_DIR}"
    REQUIRES ${ESP32_MQUICKJS_PUBLIC_REQUIRES}
    PRIV_REQUIRES ${ESP32_MQUICKJS_PRIV_REQUIRES}
)

if(NOT CMAKE_SCRIPT_MODE_FILE)
    add_dependencies(${COMPONENT_LIB} update_mquickjs_headers)
endif()

foreach(esp32_mquickjs_feature_config ${ESP32_MQUICKJS_CONFIG_FEATURE_SYMBOLS})
    if(NOT (DEFINED ${esp32_mquickjs_feature_config} AND ${esp32_mquickjs_feature_config}))
        target_compile_definitions(${COMPONENT_LIB} PRIVATE ${esp32_mquickjs_feature_config}=0)
    endif()
endforeach()

target_compile_definitions(${COMPONENT_LIB} PRIVATE _GNU_SOURCE)
target_compile_options(
    ${COMPONENT_LIB}
    PRIVATE
        -fno-math-errno
        -fno-trapping-math
        -Wno-error=format
        -Wno-error=type-limits
)
