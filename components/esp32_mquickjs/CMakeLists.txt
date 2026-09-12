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
    CONFIG_ESP32_MQUICKJS_FEATURE_NET
    CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
    CONFIG_ESP32_MQUICKJS_FEATURE_ESPNOW
    CONFIG_ESP32_MQUICKJS_FEATURE_WIFI_CSI
    CONFIG_ESP32_MQUICKJS_FEATURE_BLE
    CONFIG_ESP32_MQUICKJS_FEATURE_TLS
    CONFIG_ESP32_MQUICKJS_FEATURE_SOCKET
    CONFIG_ESP32_MQUICKJS_FEATURE_RPC
    CONFIG_ESP32_MQUICKJS_FEATURE_HTTP
    CONFIG_ESP32_MQUICKJS_FEATURE_HTTP_SERVER
    CONFIG_ESP32_MQUICKJS_FEATURE_USB_SERIAL
    CONFIG_ESP32_MQUICKJS_FEATURE_WEBSOCKET
    CONFIG_ESP32_MQUICKJS_FEATURE_BITMAP
    CONFIG_ESP32_MQUICKJS_FEATURE_BITMAP_JPEG
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

# ROM table generation must use the same SDK gates as the target JS bindings.
list(APPEND MQUICKJS_HOST_CFLAGS "-I${CMAKE_CURRENT_LIST_DIR}/internal")
foreach(esp32_mquickjs_ftm_config CONFIG_ESP_WIFI_FTM_ENABLE CONFIG_ESP_WIFI_FTM_INITIATOR_SUPPORT
        CONFIG_ESP_WIFI_FTM_RESPONDER_SUPPORT CONFIG_ESP_WIFI_SOFTAP_SUPPORT
        CONFIG_ESP_WIFI_RRM_SUPPORT CONFIG_ESP_WIFI_WNM_SUPPORT CONFIG_ESP_WIFI_11R_SUPPORT
        CONFIG_ESP_WIFI_ENTERPRISE_SUPPORT CONFIG_SOC_WIFI_HE_SUPPORT CONFIG_IDF_TARGET_ESP32C5
        CONFIG_ESP_NETIF_USES_TCPIP_WITH_BSD_API CONFIG_LWIP_IPV4
        CONFIG_ESP_WIFI_WPS_SOFTAP_REGISTRAR CONFIG_ESP_WIFI_DPP_SUPPORT CONFIG_ESP_WIFI_NAN_SYNC_ENABLE CONFIG_ESP_WIFI_NAN_USD_ENABLE
        CONFIG_ESP_WIFI_NAN_PAIRING CONFIG_ESP_WIFI_WAPI_PSK CONFIG_SOC_WIFI_MESH_SUPPORT
        CONFIG_IDF_TARGET_ESP32C3 CONFIG_IDF_TARGET_ESP32S3 CONFIG_ESP_HOST_WIFI_ENABLED)
    esp32_mquickjs_append_host_feature_define(${esp32_mquickjs_ftm_config})
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
            "${CMAKE_CURRENT_LIST_DIR}/internal/esp32_mquickjs_wifi_mesh_feature.h"
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
    "src/core/esp32_mquickjs_future_runtime_resources.c"
    "src/core/esp32_mquickjs_future_scheduler.c"
    "src/core/esp32_mquickjs_future_timeout.c"
    "src/core/esp32_mquickjs_timer_resource.c"
    "src/core/esp32_mquickjs_event_queue.c"
    "src/core/esp32_mquickjs_event_queue_drain.c"
    "src/core/esp32_mquickjs_event_queue_resources.c"
    "src/core/esp32_mquickjs_fs_atomic_write.c"
    "src/core/esp32_mquickjs_byte_span.c"
    "src/core/esp32_mquickjs_byte_source.c"
    "src/core/esp32_mquickjs_options.c"
    "src/core/esp32_mquickjs_memory.c"
    "src/core/esp32_mquickjs_memory_rtos.c"
    "src/core/esp32_mquickjs_memory_budget.c"
    "src/core/esp32_mquickjs_memory_dma_accounting.c"
    "src/core/esp32_mquickjs_memory_owner_accounting.c"
    "src/core/esp32_mquickjs_dma_transfer.c"
    "src/core/esp32_mquickjs_peripheral_lease.c"
    "src/core/esp32_mquickjs_native_pool.c"
    "src/core/esp32_mquickjs_native_lease.c"
    "src/core/esp32_mquickjs_reaper.c"
    "src/core/esp32_mquickjs_wireless_core.c"
    "src/core/esp32_mquickjs_profile.c"
    "src/core/esp32_mquickjs_sys.c"
    "src/core/esp32_mquickjs_stream.c"
    "src/core/esp32_mquickjs_request_response.c"
    "src/modules/fs/esp32_mquickjs_fs_stub.c"
    "src/modules/nvs/esp32_mquickjs_nvs_flash_boot.c"
    "src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c"
    "src/modules/wifi_radio/esp32_mquickjs_wifi_interval.c"
    "src/modules/wifi_radio/esp32_mquickjs_wifi_policy.c"
    "src/modules/wifi_radio/esp32_mquickjs_wifi_he_statistics_sdk.c"
    "src/modules/wifi_radio/esp32_mquickjs_wifi_wait.c"
    "src/modules/wifi_driver/esp32_mquickjs_wifi_tx_rate.c"
    "src/modules/wifi_driver/esp32_mquickjs_wifi_tx_rate_sdk.c"
    "src/modules/wifi_driver/esp32_mquickjs_wifi_driver.c"
    "src/modules/wifi_action/esp32_mquickjs_wifi_roc_session.c"
    "src/modules/wifi_action/esp32_mquickjs_wifi_roc.c"
    "src/modules/wifi_action/esp32_mquickjs_wifi_action.c"
    "src/modules/wifi_common/esp32_mquickjs_wifi_recovery.c"
    "src/modules/wifi_ftm/esp32_mquickjs_wifi_ftm_session.c"
    "src/modules/wifi_ftm/esp32_mquickjs_wifi_ftm_offset.c"
    "src/modules/wifi_ftm/esp32_mquickjs_wifi_ftm_module.c"
    "src/modules/wifi_ftm/esp32_mquickjs_wifi_ftm.c"
    "src/modules/wifi_action/esp32_mquickjs_wifi_action_lane.c"
    "src/modules/wifi_action/esp32_mquickjs_wifi_action_sdk.c"
    "src/modules/wifi_action/esp32_mquickjs_wifi_chm_timer.c"
    "src/modules/wifi_action/esp32_mquickjs_wifi_offchan_frame.c"
    "src/modules/wifi_roaming/esp32_mquickjs_wifi_roaming.c"
    "src/modules/wifi_roaming/esp32_mquickjs_wifi_roaming_sdk.c"
    "src/modules/wifi_roaming/esp32_mquickjs_wifi_rrm_sdk.c"
    "src/modules/wifi_roaming/esp32_mquickjs_wifi_rrm_request.c"
    "src/modules/wifi_roaming/esp32_mquickjs_wifi_neighbor_request.c"
    "src/modules/wifi_smartconfig/esp32_mquickjs_wifi_smartconfig_events.c"
    "src/modules/wifi_smartconfig/esp32_mquickjs_wifi_smartconfig_timer.c"
    "src/modules/wifi_smartconfig/esp32_mquickjs_wifi_smartconfig_timer_sdk.c"
    "src/modules/wifi_smartconfig/esp32_mquickjs_wifi_smartconfig_decoder.c"
    "src/modules/wifi_smartconfig/esp32_mquickjs_wifi_smartconfig_session.c"
    "src/modules/wifi_smartconfig/esp32_mquickjs_wifi_smartconfig.c"
    "src/modules/wifi_wps/esp32_mquickjs_wifi_wps_worker.c"
    "src/modules/wifi_wps/esp32_mquickjs_wifi_wps_ap_worker.c"
    "src/modules/wifi_dpp/esp32_mquickjs_wifi_dpp_worker.c"
    "src/modules/wifi_dpp/esp32_mquickjs_wifi_dpp_session.c"
    "src/modules/wifi_nan/esp32_mquickjs_wifi_nan.c"
    "src/modules/wifi_nan/esp32_mquickjs_wifi_nan_session.c"
    "src/modules/wifi_nan/esp32_mquickjs_wifi_nan_tx.c"
    "src/modules/wifi_mesh/esp32_mquickjs_wifi_mesh_sdk.c"
    "src/modules/wifi_mesh/esp32_mquickjs_wifi_mesh_session.c"
    "src/modules/wifi_mesh/esp32_mquickjs_wifi_mesh.c"
    "src/modules/wifi_dpp/esp32_mquickjs_wifi_dpp.c"
    "src/modules/wifi_dpp/esp32_mquickjs_wifi_dpp_connection.c"
    "src/modules/wifi_wps/esp32_mquickjs_wifi_wps_ap_session.c"
    "src/modules/wifi_wps/esp32_mquickjs_wifi_wps_session.c"
    "src/modules/wifi_wps/esp32_mquickjs_wifi_wps.c"
    "src/modules/wifi_wps/esp32_mquickjs_wifi_wps_ap.c"
    "src/modules/wifi_enterprise/esp32_mquickjs_wifi_enterprise_profile.c"
    "src/modules/wifi_enterprise/esp32_mquickjs_wifi_eap_sdk.c"
    "src/modules/wifi_enterprise/esp32_mquickjs_wifi_eap_install.c"
    "src/modules/wifi_enterprise/esp32_mquickjs_wifi_eap_options.c"
    "src/modules/wifi_enterprise/esp32_mquickjs_wifi_eap_config.c"
    "src/modules/wifi_enterprise/esp32_mquickjs_wifi_enterprise.c"
    "src/modules/wifi/esp32_mquickjs_wifi_wapi_sdk.c"
    "src/modules/wifi_twt/esp32_mquickjs_wifi_twt.c"
    "src/modules/wifi_twt/esp32_mquickjs_wifi_twt_broadcast.c"
    "src/modules/wifi_twt/esp32_mquickjs_wifi_twt_broadcast_timer.c"
    "src/modules/wifi_twt/esp32_mquickjs_wifi_twt_broadcast_event.c"
    "src/modules/wifi_twt/esp32_mquickjs_wifi_twt_broadcast_rx.c"
    "src/modules/wifi_twt/esp32_mquickjs_wifi_twt_broadcast_submit.c"
    "src/modules/wifi_twt/esp32_mquickjs_wifi_twt_broadcast_retire.c"
    "src/modules/wifi_twt/esp32_mquickjs_wifi_twt_agreement.c"
    "src/modules/wifi_twt/esp32_mquickjs_wifi_twt_close.c"
    "src/modules/wifi_twt/esp32_mquickjs_wifi_twt_options.c"
    "src/modules/wifi_twt/esp32_mquickjs_wifi_twt_lane.c"
    "src/modules/wifi_twt/esp32_mquickjs_wifi_twt_sdk.c"
    "src/modules/wifi_twt/esp32_mquickjs_wifi_twt_fence.c"
    "src/modules/wifi_twt/esp32_mquickjs_wifi_twt_tx.c"
    "src/modules/wifi_twt/esp32_mquickjs_wifi_twt_probe_timer.c"
    "src/modules/wifi_twt/esp32_mquickjs_wifi_twt_setup_timer.c"
    "src/modules/wifi_twt/esp32_mquickjs_wifi_twt_information_timer.c"
    "src/modules/wifi_twt/esp32_mquickjs_wifi_twt_information.c"
    "src/modules/wifi_twt/esp32_mquickjs_wifi_twt_setup_result.c"
    "src/modules/wifi_twt/esp32_mquickjs_wifi_twt_setup_submit.c"
    "src/modules/wifi_twt/esp32_mquickjs_wifi_twt_setup_retire.c"
    "src/modules/wifi_twt/esp32_mquickjs_wifi_twt_teardown_tx.c"
    "src/modules/wifi_twt/esp32_mquickjs_wifi_twt_probe_result.c"
    "src/modules/wifi_twt/esp32_mquickjs_wifi_twt_probe_wake.c"
    "src/modules/wifi_twt/esp32_mquickjs_wifi_twt_probe_retire.c"
    "src/modules/wifi_vendor_ie/esp32_mquickjs_wifi_vendor_ie.c"
    "src/modules/wifi_vendor_ie/esp32_mquickjs_wifi_vendor_ie_broker.c"
    "src/modules/wifi_vendor_ie/esp32_mquickjs_wifi_vendor_ie_watch.c"
    "src/modules/wifi_common/esp32_mquickjs_wifi_rx.c"
    "src/modules/wifi_common/esp32_mquickjs_wifi_neighbor.c"
    "src/modules/wifi_raw_tx/esp32_mquickjs_wifi_raw_tx_validate.c"
    "src/modules/wifi_raw_tx/esp32_mquickjs_wifi_raw_tx_snapshot.c"
    "src/modules/wifi_raw_tx/esp32_mquickjs_wifi_raw_tx_broker.c"
    "src/modules/wifi_raw_tx/esp32_mquickjs_wifi_raw_tx.c"
    "src/modules/wifi_raw_tx/esp32_mquickjs_wifi_raw_tx_queue.c"
    "src/modules/wifi_raw_tx/esp32_mquickjs_wifi_raw_tx_periodic.c"
    "src/modules/wifi_raw_tx/esp32_mquickjs_wifi_raw_tx_periodic_job.c"
    "src/modules/wifi_raw_tx/esp32_mquickjs_wifi_raw_tx_lane.c"
    "src/modules/wifi_raw_tx/esp32_mquickjs_wifi_raw_tx_session.c"
    "src/modules/wifi_raw_tx/esp32_mquickjs_wifi_raw_tx_public_session.c"
    "src/modules/wifi_raw_tx/esp32_mquickjs_wifi_raw_tx_public_periodic.c"
    "src/modules/wifi_common/esp32_mquickjs_wifi_rx_wire.c"
    "src/modules/wifi_common/esp32_mquickjs_wifi_rx_wire_metadata.c"
    "src/modules/wifi_common/esp32_mquickjs_wifi_rx_target.c"
    "src/modules/wifi_common/esp32_mquickjs_wifi_rx_filter.c"
    "src/modules/wifi_common/esp32_mquickjs_wifi_promiscuous_broker.c"
    "src/modules/wifi_common/esp32_mquickjs_wifi_promiscuous_driver.c"
    "src/modules/wifi_monitor/esp32_mquickjs_wifi_monitor_resources.c"
    "src/modules/wifi_monitor/esp32_mquickjs_wifi_monitor_wire.c"
    "src/modules/wifi_monitor/esp32_mquickjs_wifi_monitor_queue.c"
    "src/modules/wifi_monitor/esp32_mquickjs_wifi_monitor_capture.c"
    "src/modules/wifi_monitor/esp32_mquickjs_wifi_monitor_session.c"
    "src/modules/wifi_monitor/esp32_mquickjs_wifi_monitor_options.c"
    "src/modules/wifi_monitor/esp32_mquickjs_wifi_monitor_metadata.c"
    "src/modules/wifi_monitor/esp32_mquickjs_wifi_monitor.c"
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
    wpa_supplicant
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
    bt
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
    "src/modules/ledc/esp32_mquickjs_ledc_resources.c"
    "src/modules/ledc/esp32_mquickjs_ledc.c"
    PRIV_REQUIRES
        esp_driver_gpio
        esp_driver_ledc
)

esp32_mquickjs_append_feature_component(
    CONFIG_ESP32_MQUICKJS_FEATURE_ADC
    "src/modules/adc/esp32_mquickjs_adc.c"
    "src/modules/adc/esp32_mquickjs_adc_unit_resources.c"
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
    "src/modules/i2c/esp32_mquickjs_i2c_bus_resources.c"
    PRIV_REQUIRES
        esp_driver_gpio
        esp_driver_i2c
)

esp32_mquickjs_append_feature_component(
    CONFIG_ESP32_MQUICKJS_FEATURE_SPI
    "src/modules/spi/esp32_mquickjs_spi.c"
    "src/modules/spi/esp32_mquickjs_spi_native_resources.c"
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
    "src/modules/uart/esp32_mquickjs_uart_port_resources.c"
    PRIV_REQUIRES
        esp_driver_gpio
        esp_driver_uart
)

esp32_mquickjs_append_feature_component(
    CONFIG_ESP32_MQUICKJS_FEATURE_RMT
    "src/modules/rmt/esp32_mquickjs_rmt.c"
    "src/modules/rmt/esp32_mquickjs_rmt_channel_resources.c"
    "src/modules/rmt/esp32_mquickjs_rmt_symbol_buffer_resources.c"
    PRIV_REQUIRES
        esp_driver_gpio
        esp_driver_rmt
)

esp32_mquickjs_append_feature_component(
    CONFIG_ESP32_MQUICKJS_FEATURE_I2S
    "src/modules/i2s/esp32_mquickjs_i2s.c"
    "src/modules/i2s/esp32_mquickjs_i2s_channel_resources.c"
    "src/modules/i2s/esp32_mquickjs_i2s_timer_resources.c"
    PRIV_REQUIRES
        esp_driver_gpio
        esp_driver_i2s
)

if(IDF_TARGET STREQUAL "esp32s3")
    esp32_mquickjs_append_feature_component(
        CONFIG_ESP32_MQUICKJS_FEATURE_CAMERA
        "src/modules/camera/esp32_mquickjs_camera.c"
        "src/modules/camera/esp32_mquickjs_camera_driver_resources.c"
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
    CONFIG_ESP32_MQUICKJS_FEATURE_NET
    "src/modules/net/esp32_mquickjs_net.c"
    PRIV_REQUIRES
        esp_event
        esp_netif
)

esp32_mquickjs_append_feature_component(
    CONFIG_ESP32_MQUICKJS_FEATURE_HTTP
    "src/modules/http/esp32_mquickjs_http_client_resources.c"
    "src/modules/http/esp32_mquickjs_http_operation_resources.c"
    "src/modules/http/esp32_mquickjs_http.c"
    "src/modules/http/esp32_mquickjs_http_future.c"
    PRIV_REQUIRES
        esp_http_client
        mbedtls
)

esp32_mquickjs_append_feature_component(
    CONFIG_ESP32_MQUICKJS_FEATURE_HTTP_SERVER
    "src/modules/http/esp32_mquickjs_http_server_resources.c"
    "src/modules/http/esp32_mquickjs_http_server.c"
    PRIV_REQUIRES
        esp_http_server
        esp_netif
)

esp32_mquickjs_append_feature_component(
    CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
    "src/modules/wifi/esp32_mquickjs_wifi.c"
    "src/modules/wifi/esp32_mquickjs_wifi_capabilities.c"
    "src/modules/wifi/esp32_mquickjs_wifi_diagnostics.c"
    "src/modules/wifi/esp32_mquickjs_wifi_config.c"
    "src/modules/wifi/esp32_mquickjs_wifi_ap.c"
    "src/modules/wifi/esp32_mquickjs_wifi_ap_prestart.c"
    "src/modules/wifi_driver/esp32_mquickjs_wifi_antenna.c"
    "src/modules/wifi/esp32_mquickjs_wifi_netif.c"
    "src/modules/wifi/esp32_mquickjs_wifi_wake.c"
    "src/modules/wifi/esp32_mquickjs_wifi_watch.c"
    "src/modules/wifi/esp32_mquickjs_wifi_future.c"
    "src/modules/wifi/esp32_mquickjs_wifi_runtime_resources.c"
    PRIV_REQUIRES
        esp_event
        esp_netif
        esp_wifi
        esp_phy
        ieee802154
        nvs_flash
)

esp32_mquickjs_append_feature_component(
    CONFIG_ESP32_MQUICKJS_FEATURE_ESPNOW
    "src/modules/espnow/esp32_mquickjs_espnow.c"
    "src/modules/espnow/esp32_mquickjs_espnow_tx_queue.c"
    PRIV_REQUIRES
        esp_wifi
)

esp32_mquickjs_append_feature_component(
    CONFIG_ESP32_MQUICKJS_FEATURE_WIFI_CSI
    "src/modules/wifi_csi/esp32_mquickjs_wifi_csi_resources.c"
    "src/modules/wifi_csi/esp32_mquickjs_wifi_csi_store.c"
    "src/modules/wifi_csi/esp32_mquickjs_wifi_csi_packet.c"
    "src/modules/wifi_csi/esp32_mquickjs_wifi_csi_rx_span.c"
    "src/modules/wifi_csi/esp32_mquickjs_wifi_csi_rx_native.c"
    "src/modules/wifi_csi/esp32_mquickjs_wifi_csi_wire.c"
    "src/modules/wifi_csi/esp32_mquickjs_wifi_csi_batch.c"
    "src/modules/wifi_csi/esp32_mquickjs_wifi_csi_target_config.c"
    "src/modules/wifi_csi/esp32_mquickjs_wifi_csi_target_layout.c"
    "src/modules/wifi_csi/esp32_mquickjs_wifi_csi_target_legacy.c"
    "src/modules/wifi_csi/esp32_mquickjs_wifi_csi_target_he.c"
    "src/modules/wifi_csi/esp32_mquickjs_wifi_csi.c"
    PRIV_REQUIRES
        esp_wifi
)

esp32_mquickjs_append_feature_component(
    CONFIG_ESP32_MQUICKJS_FEATURE_BLE
    "src/modules/ble/esp32_mquickjs_ble_runtime_resources.c"
    "src/modules/ble/esp32_mquickjs_ble.c"
    PRIV_REQUIRES
        bt
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
    "src/modules/usb_serial/esp32_mquickjs_usb_serial_future_lifecycle.c"
    "src/modules/usb_serial/esp32_mquickjs_usb_serial.c"
    PRIV_REQUIRES
        esp_driver_usb_serial_jtag
)

if(DEFINED CONFIG_ESP32_MQUICKJS_FEATURE_USB_SERIAL AND CONFIG_ESP32_MQUICKJS_FEATURE_USB_SERIAL)
    list(APPEND ESP32_MQUICKJS_PRIV_REQUIRES esp_driver_usb_serial_jtag)
endif()

esp32_mquickjs_append_feature_component(
    CONFIG_ESP32_MQUICKJS_FEATURE_WEBSOCKET
    "src/modules/websocket/esp32_mquickjs_websocket_client_resources.c"
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
    CONFIG_ESP32_MQUICKJS_FEATURE_BITMAP_JPEG
    "src/modules/bitmap/esp32_mquickjs_bitmap_jpeg.c"
    "src/modules/bitmap/esp32_mquickjs_bitmap_jpeg_core.c"
    PRIV_REQUIRES
        espressif__esp_jpeg
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
if(CONFIG_ESP32_MQUICKJS_FEATURE_WIFI)
    list(APPEND ESP32_MQUICKJS_PRIV_INCLUDE_DIRS "$ENV{IDF_PATH}/components/esp_netif/private_include")
endif()
if(CONFIG_ESP32_MQUICKJS_FEATURE_WIFI AND (CONFIG_ESP_WIFI_RRM_SUPPORT OR
        CONFIG_ESP_WIFI_WNM_SUPPORT OR CONFIG_ESP_WIFI_ENTERPRISE_SUPPORT))
    list(APPEND ESP32_MQUICKJS_PRIV_INCLUDE_DIRS "$ENV{IDF_PATH}/components/wpa_supplicant/src")
endif()
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

if(CONFIG_ESP32_MQUICKJS_FEATURE_WIFI)
    if(CONFIG_ESP_WIFI_NAN_SYNC_ENABLE)
        target_link_libraries(${COMPONENT_LIB} INTERFACE
            "-Wl,--undefined=esp32qjs_wifi_nan_alloc_sdf" "-Wl,--wrap=nan_sdf_txcb"
            "-Wl,--wrap=nan_start_publish_service" "-Wl,--wrap=nan_start_subscribe_service"
            "-Wl,--wrap=nan_send_followup_msg" "-Wl,--wrap=nan_naf_txcb"
            "-Wl,--undefined=esp32qjs_wifi_nan_alloc_action" "-Wl,--wrap=nan_datapath_send_req"
            "-Wl,--wrap=nan_rx_naf" "-Wl,--wrap=nan_nulldata_txcb"
            "-Wl,--wrap=nan_dp_post_tx"
            "-Wl,--wrap=nan_ndp_setup_timeout_process" "-Wl,--wrap=nan_ndp_inactivity_timeout_process")
        if(CONFIG_ESP_WIFI_NAN_PAIRING)
            target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=ieee80211_mgmt_output")
        endif()
    endif()
    if(CONFIG_ESP_NETIF_USES_TCPIP_WITH_BSD_API AND CONFIG_LWIP_IPV4)
        target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=esp_event_post")
    endif()
    if(CONFIG_ESP_WIFI_DPP_SUPPORT OR CONFIG_ESP_WIFI_NAN_USD_ENABLE OR (CONFIG_ESP_NETIF_USES_TCPIP_WITH_BSD_API AND CONFIG_LWIP_IPV4))
        target_link_libraries(${COMPONENT_LIB} INTERFACE
            "-Wl,--wrap=ets_timer_setfn" "-Wl,--wrap=ets_timer_disarm"
            "-Wl,--wrap=ets_timer_done" "-Wl,--wrap=ets_timer_arm_us"
            "-Wl,--wrap=ets_timer_arm")
    endif()
    target_link_libraries(${COMPONENT_LIB} INTERFACE
        "-Wl,--wrap=wifi_action_tx_process" "-Wl,--wrap=wifi_roc_process")
    if(CONFIG_ESP_WIFI_DPP_SUPPORT OR CONFIG_ESP_WIFI_NAN_USD_ENABLE)
        # The imported native archive is scanned after the framework archive.
        target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--undefined=esp32qjs_wifi_offchan_frame_post"
            "-Wl,--undefined=esp32qjs_wifi_chm_record_reset"
            "-Wl,--wrap=chm_start_op" "-Wl,--wrap=chm_end_op_timeout_process")
    endif()
    if((CONFIG_ESP_WIFI_DPP_SUPPORT OR CONFIG_ESP_WIFI_NAN_USD_ENABLE) AND NOT (CONFIG_SOC_WIFI_HE_SUPPORT AND CONFIG_IDF_TARGET_ESP32C5))
        target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=esf_buf_recycle")
    endif()
    if(CONFIG_SOC_WIFI_HE_SUPPORT AND CONFIG_IDF_TARGET_ESP32C5)
        target_link_libraries(${COMPONENT_LIB} INTERFACE
            "-Wl,--wrap=wifi_sta_itwt_setup_process" "-Wl,--wrap=wifi_sta_btwt_setup_process"
            "-Wl,--wrap=ht_action_output" "-Wl,--wrap=esf_buf_recycle"
            "-Wl,--wrap=wifi_sta_itwt_send_probe_req_process" "-Wl,--wrap=ic_tx_pkt"
            "-Wl,--wrap=itwt_probe_rc_tx_cb" "-Wl,--wrap=he_twt_setup_txcb"
            "-Wl,--wrap=he_twt_teardown_txcb"
            "-Wl,--wrap=ieee80211_itwt_information"
            "-Wl,--wrap=he_twt_information_txcb"
            "-Wl,--wrap=esp_coex_common_timer_setfn_wrapper"
            "-Wl,--wrap=esp_coex_common_timer_disarm_wrapper"
            "-Wl,--wrap=esp_coex_common_timer_done_wrapper"
            "-Wl,--wrap=esp_coex_common_timer_arm_us_wrapper" "-Wl,--wrap=ets_timer_arm"
            "-Wl,--wrap=itwt_probe_timeout_fn_process"
            "-Wl,--wrap=itwt_setup_timeout_fn_process" "-Wl,--wrap=itwt_setup_dwell_timeout_fn_process"
            "-Wl,--wrap=itwt_information_timeout_fn_process"
            "-Wl,--wrap=btwt_setup_timeout_fn_process" "-Wl,--wrap=btwt_setup_dwell_timeout_fn_process"
            "-Wl,--wrap=ieee80211_close_all_twt_sessions" "-Wl,--wrap=ieee80211_btwt_setup" "-Wl,--wrap=he_recv_action_twt_setup"
            # Only the later SDK archive references this function-table wrapper.
            "-Wl,--undefined=__wrap_he_recv_action_twt_setup"
            "-Wl,--wrap=wifi_event_post")
    endif()
endif()
