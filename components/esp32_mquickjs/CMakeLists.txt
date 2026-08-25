set(MQUICKJS_DIR "${CMAKE_CURRENT_LIST_DIR}/vendor/mquickjs")
set(MQUICKJS_GENERATED_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(MQUICKJS_HOST_BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/host_tools")
set(MQUICKJS_HOST_TOOL "${MQUICKJS_HOST_BUILD_DIR}/mqjs_stdlib")
set(MQUICKJS_RUN_TOOL_TO_FILE_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/cmake/run_tool_to_file.cmake")
set(MQUICKJS_PATCH_BUILD_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/cmake/patch_mquickjs_build.cmake")
set(MQUICKJS_STDLIB_SOURCE "${CMAKE_CURRENT_LIST_DIR}/src/core/mqjs_stdlib_esp32.c")
set(MQUICKJS_HOST_BUILD_SOURCE "${MQUICKJS_HOST_BUILD_DIR}/mquickjs_build.host.c")
set(MQUICKJS_PROFILE_CONSTANTS_INCLUDE
    "${MQUICKJS_GENERATED_BINARY_DIR}/esp32qjs_profile_constants.inc")
set(ESP32_MQUICKJS_CONFIG_FEATURE_SYMBOLS
    CONFIG_ESP32_MQUICKJS_FEATURE_FS
    CONFIG_ESP32_MQUICKJS_FEATURE_NVS
    CONFIG_ESP32_MQUICKJS_FEATURE_GPIO
    CONFIG_ESP32_MQUICKJS_FEATURE_LEDC
    CONFIG_ESP32_MQUICKJS_FEATURE_ADC
    CONFIG_ESP32_MQUICKJS_FEATURE_DAC
    CONFIG_ESP32_MQUICKJS_FEATURE_I2C
    CONFIG_ESP32_MQUICKJS_FEATURE_SPI
    CONFIG_ESP32_MQUICKJS_FEATURE_UART
    CONFIG_ESP32_MQUICKJS_FEATURE_RMT
    CONFIG_ESP32_MQUICKJS_FEATURE_I2S
    CONFIG_ESP32_MQUICKJS_FEATURE_CAMERA
    CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
    CONFIG_ESP32_MQUICKJS_FEATURE_TLS
    CONFIG_ESP32_MQUICKJS_FEATURE_SOCKET
    CONFIG_ESP32_MQUICKJS_FEATURE_RPC
    CONFIG_ESP32_MQUICKJS_FEATURE_HTTP
    CONFIG_ESP32_MQUICKJS_FEATURE_HTTP_SERVER
    CONFIG_ESP32_MQUICKJS_FEATURE_USB_SERIAL
    CONFIG_ESP32_MQUICKJS_FEATURE_WEBSOCKET
    CONFIG_ESP32_MQUICKJS_FEATURE_BITMAP
    CONFIG_ESP32_MQUICKJS_FEATURE_RUNTIME_LOGS
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

if(DEFINED CONFIG_SOC_SPI_PERIPH_NUM)
    list(APPEND MQUICKJS_HOST_CFLAGS "-DCONFIG_SOC_SPI_PERIPH_NUM=${CONFIG_SOC_SPI_PERIPH_NUM}")
endif()

set(MQUICKJS_ATOM_HEADER "${MQUICKJS_GENERATED_BINARY_DIR}/mquickjs_atom.h")
set(MQUICKJS_STDLIB_HEADER "${MQUICKJS_GENERATED_BINARY_DIR}/mqjs_stdlib.h")

file(MAKE_DIRECTORY "${MQUICKJS_GENERATED_BINARY_DIR}")

if(DEFINED ESP32QJS_PROFILE_CONSTANTS_FILE AND
   NOT "${ESP32QJS_PROFILE_CONSTANTS_FILE}" STREQUAL "")
    get_filename_component(_esp32qjs_profile_constants_file
        "${ESP32QJS_PROFILE_CONSTANTS_FILE}" ABSOLUTE BASE_DIR "${CMAKE_SOURCE_DIR}")
    if(NOT EXISTS "${_esp32qjs_profile_constants_file}")
        message(FATAL_ERROR
            "ESP32QJS profile constants file does not exist: ${_esp32qjs_profile_constants_file}")
    endif()
    configure_file(
        "${_esp32qjs_profile_constants_file}"
        "${MQUICKJS_PROFILE_CONSTANTS_INCLUDE}"
        COPYONLY)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${_esp32qjs_profile_constants_file}")
else()
    file(WRITE "${MQUICKJS_PROFILE_CONSTANTS_INCLUDE}"
        "/* No hardware-profile constants configured. */\n")
endif()

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

    add_custom_target(update_mquickjs_headers
        DEPENDS mquickjs_generated_headers
    )
endif()

set(ESP32_MQUICKJS_SRCS
    "src/core/esp32_mquickjs.c"
    "src/core/esp32_mquickjs_future.c"
    "src/core/esp32_mquickjs_event_queue.c"
    "src/core/esp32_mquickjs_event_queue_drain.c"
    "src/core/esp32_mquickjs_byte_span.c"
    "src/core/esp32_mquickjs_byte_source.c"
    "src/core/esp32_mquickjs_memory.c"
    "src/core/esp32_mquickjs_peripheral_lease.c"
    "src/core/esp32_mquickjs_profile.c"
    "src/core/esp32_mquickjs_sys.c"
    "src/core/esp32_mquickjs_stream.c"
    "src/core/esp32_mquickjs_request_response.c"
    "src/modules/time/esp32_mquickjs_time.c"
    "src/core/js_stdlib.c"
    "${MQUICKJS_DIR}/cutils.c"
    "${MQUICKJS_DIR}/dtoa.c"
    "${MQUICKJS_DIR}/libm.c"
    "${MQUICKJS_DIR}/mquickjs.c"
)

# Keep upstream diagnostics from weakening warnings for adapter sources. These
# two files contain host-width assumptions in debug-only formatting paths.
if(NOT CMAKE_SCRIPT_MODE_FILE)
    set_source_files_properties(
        "${MQUICKJS_DIR}/dtoa.c"
        PROPERTIES COMPILE_OPTIONS "-Wno-format"
    )
    set_source_files_properties(
        "${MQUICKJS_DIR}/mquickjs.c"
        PROPERTIES COMPILE_OPTIONS "-Wno-format;-Wno-type-limits;-Wno-dangling-pointer"
    )
endif()

set(ESP32_MQUICKJS_PUBLIC_REQUIRES
    esp_common
)

set(ESP32_MQUICKJS_PRIV_REQUIRES
    bootloader_support
    esp_timer
    mbedtls
    spi_flash
    esp_psram
    # IDF resolves component requirements during early expansion, before an
    # external application's feature sdkconfig is reliably available. Keep the
    # possible feature dependencies in the component graph unconditionally;
    # source files and linked code remain gated by their Kconfig symbols.
    joltwallet__littlefs
    nvs_flash
    esp_driver_gpio
    esp_driver_ledc
    esp_adc
    esp_driver_dac
    esp_driver_i2c
    esp_driver_spi
    esp_driver_uart
    esp_driver_rmt
    esp_driver_i2s
    esp_http_client
    esp_http_server
    esp_event
    esp_netif
    esp-tls
    esp_wifi
    esp_driver_usb_serial_jtag
    esp_websocket_client
    lwip
)

esp32_mquickjs_append_feature_component(
    CONFIG_ESP32_MQUICKJS_FEATURE_RPC
    "src/core/esp32_mquickjs_rpc.c"
    "src/core/esp32_mquickjs_rpc_wire.c"
    PRIV_REQUIRES
        espressif__cbor
)

esp32_mquickjs_append_feature_component(
    CONFIG_ESP32_MQUICKJS_FEATURE_NVS
    "src/modules/nvs/esp32_mquickjs_nvs.c"
    PRIV_REQUIRES
        nvs_flash
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
    CONFIG_ESP32_MQUICKJS_FEATURE_SPI
    "src/modules/spi/esp32_mquickjs_spi.c"
    PRIV_REQUIRES
        esp_driver_gpio
        esp_driver_spi
)

if(DEFINED CONFIG_ESP32_MQUICKJS_FEATURE_SPI AND CONFIG_ESP32_MQUICKJS_FEATURE_SPI)
    list(APPEND ESP32_MQUICKJS_PRIV_REQUIRES esp_driver_spi)
endif()

esp32_mquickjs_append_feature_component(
    CONFIG_ESP32_MQUICKJS_FEATURE_UART
    "src/modules/uart/esp32_mquickjs_uart.c"
    PRIV_REQUIRES
        esp_driver_gpio
        esp_driver_uart
)

esp32_mquickjs_append_feature_component(
    CONFIG_ESP32_MQUICKJS_FEATURE_RMT
    "src/modules/rmt/esp32_mquickjs_rmt.c"
    PRIV_REQUIRES
        esp_driver_gpio
        esp_driver_rmt
)

esp32_mquickjs_append_feature_component(
    CONFIG_ESP32_MQUICKJS_FEATURE_I2S
    "src/modules/i2s/esp32_mquickjs_i2s.c"
    PRIV_REQUIRES
        esp_driver_gpio
        esp_driver_i2s
)

if(IDF_TARGET STREQUAL "esp32s3")
    esp32_mquickjs_append_feature_component(
        CONFIG_ESP32_MQUICKJS_FEATURE_CAMERA
        "src/modules/camera/esp32_mquickjs_camera.c"
        PRIV_REQUIRES
            espressif__esp32-camera
            esp_driver_gpio
            esp_driver_i2c
            esp_driver_ledc
    )
endif()

if(DEFINED CONFIG_ESP32_MQUICKJS_FEATURE_UART AND CONFIG_ESP32_MQUICKJS_FEATURE_UART)
    list(APPEND ESP32_MQUICKJS_PRIV_REQUIRES esp_driver_gpio esp_driver_uart)
endif()

esp32_mquickjs_append_feature_component(
    CONFIG_ESP32_MQUICKJS_FEATURE_HTTP
    "src/modules/http/esp32_mquickjs_http.c"
    "src/modules/http/esp32_mquickjs_http_future.c"
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
    "src/modules/wifi/esp32_mquickjs_wifi_future.c"
    PRIV_REQUIRES
        esp_event
        esp_netif
        esp_wifi
        nvs_flash
)

esp32_mquickjs_append_feature_component(
    CONFIG_ESP32_MQUICKJS_FEATURE_SOCKET
    "src/modules/socket/esp32_mquickjs_socket.c"
    PRIV_REQUIRES
        esp-tls
        lwip
        mbedtls
)

if(DEFINED CONFIG_ESP32_MQUICKJS_FEATURE_TLS AND
   CONFIG_ESP32_MQUICKJS_FEATURE_TLS AND
   ((DEFINED CONFIG_ESP32_MQUICKJS_FEATURE_HTTP AND
    CONFIG_ESP32_MQUICKJS_FEATURE_HTTP) OR
   (DEFINED CONFIG_ESP32_MQUICKJS_FEATURE_SOCKET AND
    CONFIG_ESP32_MQUICKJS_FEATURE_SOCKET)))
    list(APPEND ESP32_MQUICKJS_SRCS
        "src/utils/esp32_mquickjs_tls_error.c")
endif()

esp32_mquickjs_append_feature_component(
    CONFIG_ESP32_MQUICKJS_FEATURE_USB_SERIAL
    "src/core/esp32_mquickjs_line_framer.c"
    "src/modules/usb_serial/esp32_mquickjs_usb_serial.c"
    PRIV_REQUIRES
        esp_driver_usb_serial_jtag
)

if(DEFINED CONFIG_ESP32_MQUICKJS_FEATURE_USB_SERIAL AND CONFIG_ESP32_MQUICKJS_FEATURE_USB_SERIAL)
    list(APPEND ESP32_MQUICKJS_PRIV_REQUIRES esp_driver_usb_serial_jtag)
endif()

esp32_mquickjs_append_feature_component(
    CONFIG_ESP32_MQUICKJS_FEATURE_WEBSOCKET
    "src/modules/websocket/esp32_mquickjs_websocket.c"
    PRIV_REQUIRES
        esp_websocket_client
        mbedtls
)

if(DEFINED CONFIG_ESP32_MQUICKJS_FEATURE_WEBSOCKET AND CONFIG_ESP32_MQUICKJS_FEATURE_WEBSOCKET)
    list(APPEND ESP32_MQUICKJS_PRIV_REQUIRES esp_websocket_client mbedtls)
endif()

esp32_mquickjs_append_feature_component(
    CONFIG_ESP32_MQUICKJS_FEATURE_BITMAP
    "src/modules/bitmap/esp32_mquickjs_font.c"
    "src/modules/bitmap/esp32_mquickjs_bitmap.c"
    "src/modules/bitmap/esp32_mquickjs_display_command_buffer.c"
    "src/modules/bitmap/esp32_mquickjs_bitmap_draw.c"
    "src/modules/bitmap/esp32_mquickjs_bitmap_image.c"
    "src/modules/bitmap/esp32_mquickjs_bitmap_image_core.c"
    "src/modules/bitmap/esp32_mquickjs_bitmap_font.c"
    "src/modules/bitmap/esp32_mquickjs_bitmap_text.c"
)

esp32_mquickjs_append_feature_component(
    CONFIG_ESP32_MQUICKJS_FEATURE_RUNTIME_LOGS
    "src/core/esp32_mquickjs_log_ring.c"
    "src/core/esp32_mquickjs_runtime_logs.c"
)

list(REMOVE_DUPLICATES ESP32_MQUICKJS_PRIV_REQUIRES)

set(ESP32_MQUICKJS_PRIV_INCLUDE_DIRS
    "internal"
    "${MQUICKJS_GENERATED_BINARY_DIR}"
    "${MQUICKJS_DIR}"
)
idf_component_register(
    SRCS ${ESP32_MQUICKJS_SRCS}
    INCLUDE_DIRS
        "include"
        "${MQUICKJS_DIR}"
    PRIV_INCLUDE_DIRS ${ESP32_MQUICKJS_PRIV_INCLUDE_DIRS}
    REQUIRES ${ESP32_MQUICKJS_PUBLIC_REQUIRES}
    PRIV_REQUIRES ${ESP32_MQUICKJS_PRIV_REQUIRES}
)

if(NOT CMAKE_SCRIPT_MODE_FILE)
    add_dependencies(${COMPONENT_LIB} mquickjs_generated_headers)
endif()

foreach(esp32_mquickjs_feature_config ${ESP32_MQUICKJS_CONFIG_FEATURE_SYMBOLS})
    if(NOT (DEFINED ${esp32_mquickjs_feature_config} AND ${esp32_mquickjs_feature_config}))
        target_compile_definitions(${COMPONENT_LIB} PRIVATE ${esp32_mquickjs_feature_config}=0)
    endif()
endforeach()

target_compile_definitions(${COMPONENT_LIB} PRIVATE _GNU_SOURCE)
if(CONFIG_ESP32_MQUICKJS_DEBUG_GC)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE DEBUG_GC=1)
endif()
target_compile_options(
    ${COMPONENT_LIB}
    PRIVATE
        -fno-math-errno
        -fno-trapping-math
)
