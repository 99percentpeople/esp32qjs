# Apply fixed-SDK WPS corrections only to this build's translation units.
# CONFIG_WPS is a wpa_supplicant compile definition, not a Kconfig boolean.
if(NOT TARGET idf::wpa_supplicant OR NOT CONFIG_ESP32_MQUICKJS_FEATURE_WIFI)
    return()
endif()
idf_component_get_property(_esp32qjs_wps_lib wpa_supplicant COMPONENT_LIB)
idf_component_get_property(_esp32qjs_wps_dir wpa_supplicant COMPONENT_DIR)
idf_build_get_property(_esp32qjs_wps_python PYTHON)
set(_esp32qjs_wps_script "${CMAKE_SOURCE_DIR}/scripts/sdk_patches/wpa/wps/native.py")
set(_esp32qjs_wps_output "${CMAKE_BINARY_DIR}/esp32qjs_sdk_fixes/wps")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_esp32qjs_wps_script}"
    "${CMAKE_SOURCE_DIR}/scripts/sdk_patches/wpa/wps/credentials.py"
    "${CMAKE_SOURCE_DIR}/scripts/sdk_patches/wpa/wps/errors.py"
    "${CMAKE_SOURCE_DIR}/scripts/sdk_patches/wpa/wps/registrar.py"
    "${CMAKE_SOURCE_DIR}/scripts/sdk_patches/wpa/wps/registrar_timers.py"
    "${CMAKE_SOURCE_DIR}/scripts/sdk_patches/wpa/wps/ap_result.py"
    "${CMAKE_SOURCE_DIR}/scripts/sdk_patches/wpa/wps/ap_close.py"
    "${CMAKE_SOURCE_DIR}/scripts/sdk_patches/wpa/wps/ap_init_cleanup.py"
    "${CMAKE_SOURCE_DIR}/scripts/sdk_patches/wpa/wps/ap_input.py"
    "${CMAKE_SOURCE_DIR}/scripts/sdk_patches/wpa/wps/ap_commands.py"
    "${CMAKE_SOURCE_DIR}/scripts/sdk_patches/wpa/eap/control.py"
    "${CMAKE_SOURCE_DIR}/scripts/sdk_patches/wpa/eap/lifecycle.py"
    "${CMAKE_SOURCE_DIR}/scripts/sdk_patches/wpa/eap/secrets.py"
    "${CMAKE_SOURCE_DIR}/scripts/sdk_patches/wpa/wps/eapol.py"
    "${CMAKE_SOURCE_DIR}/scripts/sdk_patches/wpa/wps/eapol_retire.py"
    "${CMAKE_SOURCE_DIR}/scripts/sdk_patches/wpa/wps/peer_delays.py"
    "${CMAKE_SOURCE_DIR}/components/esp32_mquickjs/internal/esp32_mquickjs_wifi_wps_ap_result.h"
    "${CMAKE_SOURCE_DIR}/components/esp32_mquickjs/src/modules/wifi_wps/esp32_mquickjs_wifi_wps_ap_result.inc"
    "${CMAKE_SOURCE_DIR}/components/esp32_mquickjs/internal/esp32_mquickjs_wifi_wps_ap_sdk.h"
    "${CMAKE_SOURCE_DIR}/components/esp32_mquickjs/src/modules/wifi_wps/esp32_mquickjs_wifi_wps_ap_sdk.inc"
    "${CMAKE_SOURCE_DIR}/components/esp32_mquickjs/internal/esp32_mquickjs_wifi_wps_sdk.h"
    "${CMAKE_SOURCE_DIR}/components/esp32_mquickjs/src/modules/wifi_wps/esp32_mquickjs_wifi_wps_sdk.inc")
foreach(_esp32qjs_wps_input
        "src/common/ieee802_11_common.c"
        "src/common/ieee802_11_common.h"
        "src/common/ieee802_11_defs.h"
        "src/ap/sta_info.h"
        "src/eapol_auth/eapol_auth_sm.h"
        "esp_supplicant/src/esp_wpa_main.c"
        "esp_supplicant/src/esp_wpa3.c"
        "esp_supplicant/src/esp_hostap.c"
        "src/eap_server/eap.h"
        "src/eap_server/eap_i.h"
        "src/eap_server/eap_server.c"
        "src/eapol_auth/eapol_auth_sm_i.h"
        "src/ap/sta_info.c"
        "src/eap_common/eap_wsc_common.h"
        "esp_supplicant/src/esp_hostpad_wps.c"
        "src/ap/wps_hostapd.c"
        "src/ap/ieee802_1x.c"
        "src/eap_server/eap_server_wsc.c"
        "src/ap/wps_hostapd.h"
        "src/ap/hostapd.h"
        "src/ap/ap_config.h"
        "src/ap/wpa_auth_i.h"
        "src/ap/wpa_auth.h"
        "src/eap_server/eap_server_methods.c"
        "src/eap_server/eap_server_identity.c"
        "src/eapol_auth/eapol_auth_sm.c"
        "src/wps/wps_registrar.c"
        "../esp_wifi/Kconfig"
        "src/wps/wps_attr_build.c"
        "src/wps/wps_common.c"
        "../esp_timer/src/ets_timer_legacy.c"
        "../esp_timer/src/esp_timer.c"
        "../esp_timer/include/esp_timer.h"
        "../esp_wifi/include/esp_private/wifi_os_adapter.h"
        "esp_supplicant/src/esp_wps.c" "esp_supplicant/src/esp_wps_i.h"
        "esp_supplicant/src/esp_dpp_i.h"
        "esp_supplicant/include/esp_wps.h" "src/wps/wps.c" "src/wps/wps.h"
        "src/wps/wps_i.h" "src/wps/wps_enrollee.c" "src/utils/common.c"
        "src/utils/wpabuf.c" "port/include/os.h" "CMakeLists.txt"
        "port/eloop.c" "src/utils/eloop.h" "esp_supplicant/src/esp_wifi_driver.h"
        "../esp_wifi/include/esp_wifi_types_generic.h"
        "esp_supplicant/src/esp_wpas_glue.c"
        "src/rsn_supp/wpa.c"
        "src/wps/wps_dev_attr.c"
        "../esp_wifi/include/esp_private/wifi.h"
        "../esp_wifi/lib/esp32c3/libnet80211.a"
        "../esp_wifi/lib/esp32s3/libnet80211.a"
        "../esp_wifi/lib/esp32c5/libnet80211.a"
)
    get_filename_component(_esp32qjs_wps_dependency "${_esp32qjs_wps_dir}/${_esp32qjs_wps_input}" REALPATH)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_esp32qjs_wps_dependency}")
endforeach()
execute_process(
    COMMAND "${_esp32qjs_wps_python}" "${CMAKE_SOURCE_DIR}/scripts/sdk_patch.py" wps_native
        --component "${_esp32qjs_wps_dir}" --output-dir "${_esp32qjs_wps_output}"
    RESULT_VARIABLE _esp32qjs_wps_result
    OUTPUT_VARIABLE _esp32qjs_wps_message
    ERROR_VARIABLE _esp32qjs_wps_error)
if(NOT _esp32qjs_wps_result EQUAL 0)
    message(FATAL_ERROR "ESP32QJS WPS fix failed: ${_esp32qjs_wps_error}")
endif()
get_target_property(_esp32qjs_wps_sources ${_esp32qjs_wps_lib} SOURCES)
set(_esp32qjs_wps_new_sources "")
set(_esp32qjs_wps_count 0)
foreach(_esp32qjs_wps_entry IN LISTS _esp32qjs_wps_sources)
    get_filename_component(_esp32qjs_wps_absolute "${_esp32qjs_wps_entry}" ABSOLUTE BASE_DIR "${_esp32qjs_wps_dir}")
    if(_esp32qjs_wps_absolute STREQUAL "${_esp32qjs_wps_dir}/esp_supplicant/src/esp_wps.c"
       OR _esp32qjs_wps_absolute STREQUAL "${_esp32qjs_wps_dir}/esp_supplicant/src/esp_wpa_main.c"
       OR _esp32qjs_wps_absolute STREQUAL "${CMAKE_BINARY_DIR}/esp32qjs_sdk_fixes/eap_secrets/esp_wpa_main.c"
       OR _esp32qjs_wps_absolute STREQUAL "${_esp32qjs_wps_dir}/src/wps/wps.c"
       OR _esp32qjs_wps_absolute STREQUAL "${_esp32qjs_wps_dir}/src/wps/wps_enrollee.c"
       OR _esp32qjs_wps_absolute STREQUAL "${_esp32qjs_wps_dir}/esp_supplicant/src/esp_hostpad_wps.c"
       OR _esp32qjs_wps_absolute STREQUAL "${_esp32qjs_wps_dir}/esp_supplicant/src/esp_hostap.c"
       OR _esp32qjs_wps_absolute STREQUAL "${_esp32qjs_wps_dir}/src/ap/wps_hostapd.c"
       OR _esp32qjs_wps_absolute STREQUAL "${_esp32qjs_wps_dir}/src/ap/sta_info.c"
       OR _esp32qjs_wps_absolute STREQUAL "${_esp32qjs_wps_dir}/src/ap/ieee802_1x.c"
       OR _esp32qjs_wps_absolute STREQUAL "${_esp32qjs_wps_dir}/src/eap_server/eap_server_wsc.c"
       OR _esp32qjs_wps_absolute STREQUAL "${_esp32qjs_wps_dir}/src/wps/wps_registrar.c"
       OR _esp32qjs_wps_absolute STREQUAL "${_esp32qjs_wps_dir}/src/eapol_auth/eapol_auth_sm.c"
)
        get_filename_component(_esp32qjs_wps_name "${_esp32qjs_wps_absolute}" NAME)
        list(APPEND _esp32qjs_wps_new_sources "${_esp32qjs_wps_output}/${_esp32qjs_wps_name}")
        math(EXPR _esp32qjs_wps_count "${_esp32qjs_wps_count} + 1")
    else()
        list(APPEND _esp32qjs_wps_new_sources "${_esp32qjs_wps_entry}")
    endif()
endforeach()
set(_esp32qjs_wps_expected 6)
if(CONFIG_ESP_WIFI_WPS_SOFTAP_REGISTRAR)
    set(_esp32qjs_wps_expected 11)
endif()
if(CONFIG_ESP_WIFI_SOFTAP_SUPPORT)
    math(EXPR _esp32qjs_wps_expected "${_esp32qjs_wps_expected} + 1")
endif()
if(NOT _esp32qjs_wps_count EQUAL _esp32qjs_wps_expected)
    message(FATAL_ERROR "Unexpected count of WPS source replacements in SDK component: ${_esp32qjs_wps_count}")
endif()
set_property(TARGET ${_esp32qjs_wps_lib} PROPERTY SOURCES "${_esp32qjs_wps_new_sources}")
string(STRIP "${_esp32qjs_wps_message}" _esp32qjs_wps_message)
message(STATUS "${_esp32qjs_wps_message}")
