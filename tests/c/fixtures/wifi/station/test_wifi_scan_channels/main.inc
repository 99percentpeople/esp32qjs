static void check(wifi_scan_config_t config, esp_err_t expected)
{
    wifi_scan_config_t before = config;
    wifi_country_t saved_country = country;
    assert(esp32_mquickjs_wifi_radio_validate_scan_channels(&s_radio.operation, &config) == expected);
    assert(lock_depth == 0);
    /* Admission must not trim the request, change scan mode or set a country. */
    assert(memcmp(&before, &config, sizeof(config)) == 0);
    assert(memcmp(&saved_country, &country, sizeof(country)) == 0);
}

int main(void)
{
    wifi_scan_config_t numeric = { .channel = 36 };
    wifi_scan_config_t bitmap = { .channel_bitmap = { .ghz_2_channels = 1,
        .ghz_5_channels = (1U << 1) | (1U << 5) | (1U << 21) } }; /* 36, 52, 149 */
#if TEST_IMPLICIT && CONFIG_SOC_WIFI_SUPPORT_5G
    for (int policy = WIFI_COUNTRY_POLICY_AUTO; policy <= WIFI_COUNTRY_POLICY_MANUAL; ++policy) {
        country.policy = policy;
        for (int explicit_mask = 0; explicit_mask < 2; ++explicit_mask) {
            if (policy == WIFI_COUNTRY_POLICY_MANUAL && explicit_mask) continue;
            country.wifi_5g_channel_mask = explicit_mask ? (1U << 2) : 0;
            for (int mode = WIFI_SCAN_TYPE_ACTIVE; mode <= WIFI_SCAN_TYPE_PASSIVE; ++mode) {
                numeric.scan_type = bitmap.scan_type = mode;
                check(numeric, ESP_OK);
                check(bitmap, ESP_OK);
                bitmap.channel_bitmap.ghz_2_channels = 1U << 6;
                check(bitmap, ESP_OK);
                bitmap.channel_bitmap.ghz_2_channels = 1;
            }
        }
    }
#else
    check((wifi_scan_config_t){ .channel = 6 }, ESP_OK);
    check((wifi_scan_config_t){ .channel = 14 }, ESP_ERR_NOT_ALLOWED);
    check((wifi_scan_config_t){ .channel = 35 }, ESP_ERR_INVALID_ARG);
    check((wifi_scan_config_t){ .channel = 6, .channel_bitmap.ghz_2_channels = 2 }, ESP_ERR_INVALID_ARG);
    check((wifi_scan_config_t){ .channel_bitmap = { 1, 1 } }, ESP_ERR_INVALID_ARG);
    check((wifi_scan_config_t){ .channel_bitmap.ghz_5_channels = 1U << 29 }, ESP_ERR_INVALID_ARG);
#if CONFIG_SOC_WIFI_SUPPORT_5G
    country.policy = WIFI_COUNTRY_POLICY_MANUAL;
    country.wifi_5g_channel_mask = 1U << 1;
    check(numeric, ESP_OK);
    check(bitmap, ESP_ERR_NOT_ALLOWED);
    check((wifi_scan_config_t){ .channel = 40 }, ESP_ERR_NOT_ALLOWED);
    country.wifi_5g_channel_mask = bitmap.channel_bitmap.ghz_5_channels;
    check(bitmap, ESP_OK);
    band = WIFI_BAND_MODE_2G_ONLY;
    check(numeric, ESP_ERR_NOT_ALLOWED);
    band = WIFI_BAND_MODE_5G_ONLY;
    check((wifi_scan_config_t){ .channel = 6 }, ESP_ERR_NOT_ALLOWED);
    band = WIFI_BAND_MODE_AUTO;
    band_error = 77;
    check(numeric, 77);
    band_error = 0;
    country.policy = 99;
    check(numeric, ESP_ERR_NOT_SUPPORTED);
#else
    check(numeric, ESP_ERR_NOT_SUPPORTED);
    check(bitmap, ESP_ERR_NOT_SUPPORTED);
#endif
    country_error = 88;
    check((wifi_scan_config_t){ .channel = 6 }, 88);
    check((wifi_scan_config_t){0}, ESP_OK); /* All-channel scan delegates to SDK. */
    country_error = 0;
    s_radio.lifecycle.identity = 1;
    check((wifi_scan_config_t){0}, ESP_ERR_INVALID_STATE);
    s_radio.lifecycle.identity = 0;
    s_radio.fault_stage = "cleanup";
    check((wifi_scan_config_t){0}, ESP_ERR_INVALID_STATE);
    s_radio.fault_stage = NULL;
    s_radio.started = false;
    check((wifi_scan_config_t){0}, ESP_ERR_INVALID_STATE);
    s_radio.started = true;
    esp32_mquickjs_wifi_radio_operation_t stale = s_radio.operation;
    stale.generation++;
    assert(esp32_mquickjs_wifi_radio_validate_scan_channels(&stale, &(wifi_scan_config_t){0}) == ESP_ERR_INVALID_STATE);
    assert(lock_depth == 0);
#endif
    return 0;
}
