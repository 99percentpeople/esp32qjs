/* Distinguish the native causes hidden by the public correlation-fault flag.
 * SDK metadata/descriptor faults are injected; the broker and hooks are real.
 * This reproduces the state transition, not the reported device's trigger. */
int main(void) {
    (void)start_callback; (void)finish_callback; (void)sending_token;
    uint8_t bytes[24] = {0x80};
    memset(bytes + 4, 0x34, 6); memset(bytes + 10, 0x12, 6);
    policy_t policy = {.driver_sequence = true};
    assert(esp32_mquickjs_wifi_raw_tx_broker_register(7) == ESP_OK);
    const char *reasons[] = {"invalid-interface", "interface-mismatch",
        "destination-mismatch", "source-mismatch", "orphan-callback"};
    for (unsigned fault = 0; fault < 5; ++fault) {
        memcpy(destination, bytes + 4, 6); memcpy(source_address, bytes + 10, 6);
        callback_info = (esp_80211_tx_info_t){.des_addr = destination,
            .src_addr = source_address, .ifidx = WIFI_IF_STA,
            .tx_status = WIFI_SEND_SUCCESS};
        token_t token = {0}; status_t before, after;
        esp32_mquickjs_wifi_raw_tx_validation_t validation;
        esp32_mquickjs_wifi_raw_tx_broker_status(&before);
        assert(!before.correlation_failure_reason && !before.correlation_failure_identity &&
            !before.correlation_failure_generation);
        assert(esp32_mquickjs_wifi_raw_tx_broker_submit(7, 1, bytes, sizeof(bytes),
            &policy, &token, &validation) == ESP_OK);
        assert(validation == ESP32_MQUICKJS_WIFI_RAW_TX_VALID);
        void *descriptor = last_descriptor;
        if (fault == 0) callback_info.ifidx = (wifi_interface_t)99;
        if (fault == 1) callback_info.ifidx = WIFI_IF_AP;
        if (fault == 2) destination[0] ^= 1;
        if (fault == 3) source_address[0] ^= 1;
        if (fault == 4) descriptor = &descriptors[15];
        esp_80211_tx_info_t info;
        esp32qjs_raw_tx_info(descriptor, &info);
        assert(esp32_mquickjs_wifi_raw_tx_broker_result(&token, &after));
        assert(after.submit_returned && after.driver_accepted && after.submit_error == ESP_OK);
        assert(after.operation_active && !after.driver_completed && !after.native_terminated);
        assert(after.correlation_fault && after.quarantined && after.registration_uncertain);
        assert(!strcmp(after.correlation_failure_reason, reasons[fault]));
        assert(after.correlation_failure_identity == (fault == 4 ? 0 : token.identity));
        assert(after.correlation_failure_generation == 7);
        assert(after.invalid_callbacks == before.invalid_callbacks + (fault == 0));
        assert(after.mismatched_callbacks == before.mismatched_callbacks + (fault >= 1 && fault <= 3));
        assert(after.orphan_callbacks == before.orphan_callbacks + (fault == 4));
        assert(after.duplicate_callbacks == before.duplicate_callbacks);
        assert(!esp32_mquickjs_wifi_raw_tx_broker_retire(&token));
        assert(allocations == frees + 1);
        /* Later faults cannot replace the initial cause or its attribution. */
        esp32qjs_raw_tx_info(&descriptors[15], &info);
        esp32_mquickjs_wifi_raw_tx_broker_status(&after);
        assert(!strcmp(after.correlation_failure_reason, reasons[fault]));
        assert(after.correlation_failure_identity == (fault == 4 ? 0 : token.identity));
        assert(esp32_mquickjs_wifi_raw_tx_broker_quiesce(7, &token) == ESP_OK);
        /* An established deinit fence, not unregister alone, permits reclaim. */
        assert(!esp32_mquickjs_wifi_raw_tx_broker_retire(&token));
        assert(esp32_mquickjs_wifi_raw_tx_broker_reset_after_deinit(7));
        assert(esp32_mquickjs_wifi_raw_tx_broker_retire(&token));
        assert(allocations == frees);
        esp32_mquickjs_wifi_raw_tx_broker_status(&after);
        assert(!strcmp(after.correlation_failure_reason, reasons[fault]));
        assert(after.correlation_failure_generation == 7);
        assert(esp32_mquickjs_wifi_raw_tx_broker_register(7) == ESP_OK);
        esp32_mquickjs_wifi_raw_tx_broker_status(&before);
        assert(before.orphan_callbacks == after.orphan_callbacks);
    }
    memcpy(destination, bytes + 4, 6); memcpy(source_address, bytes + 10, 6);
    callback_info = (esp_80211_tx_info_t){.des_addr = destination,
        .src_addr = source_address, .ifidx = WIFI_IF_STA, .tx_status = WIFI_SEND_SUCCESS};
    /* A second allocation reusing a live descriptor identifies the submitting
     * packet, even if the callback matches the older record first. */
    token_t tokens[2] = {{0}};
    status_t st;
    esp32_mquickjs_wifi_raw_tx_validation_t validation;
    for (unsigned i = 0; i < 2; ++i) {
        next_descriptor = 0;
        assert(esp32_mquickjs_wifi_raw_tx_broker_submit(7, 1, bytes, sizeof(bytes),
            &policy, &tokens[i], &validation) == ESP_OK);
    }
    esp32_mquickjs_wifi_raw_tx_broker_status(&st);
    assert(!strcmp(st.correlation_failure_reason, "descriptor-reused"));
    assert(st.correlation_failure_identity == tokens[1].identity);
    esp_80211_tx_info_t info;
    esp32qjs_raw_tx_info(last_descriptor, &info);
    assert(esp32_mquickjs_wifi_raw_tx_broker_result(&tokens[1], &st) && st.correlation_fault);
    assert(!strcmp(st.correlation_failure_reason, "descriptor-reused"));
    assert(esp32_mquickjs_wifi_raw_tx_broker_reset_after_deinit(7));
    for (unsigned i = 0; i < 2; ++i) assert(esp32_mquickjs_wifi_raw_tx_broker_retire(&tokens[i]));
    assert(esp32_mquickjs_wifi_raw_tx_broker_register(8) == ESP_OK);
    /* Completion before a rejected submit return must retain that contradiction. */
    synchronous = true; send_error = 81;
    assert(esp32_mquickjs_wifi_raw_tx_broker_submit(8, 1, bytes, sizeof(bytes),
        &policy, &tokens[0], &validation) == 81);
    assert(esp32_mquickjs_wifi_raw_tx_broker_result(&tokens[0], &st));
    assert(st.driver_completed && st.correlation_fault && st.submit_error == 81);
    assert(!strcmp(st.correlation_failure_reason, "completion-after-rejection"));
    assert(st.correlation_failure_identity == tokens[0].identity && st.correlation_failure_generation == 8);
    assert(!esp32_mquickjs_wifi_raw_tx_broker_retire(&tokens[0]));
    assert(esp32_mquickjs_wifi_raw_tx_broker_reset_after_deinit(8));
    assert(esp32_mquickjs_wifi_raw_tx_broker_retire(&tokens[0]));
    assert(allocations == frees);
    return 0;
}
