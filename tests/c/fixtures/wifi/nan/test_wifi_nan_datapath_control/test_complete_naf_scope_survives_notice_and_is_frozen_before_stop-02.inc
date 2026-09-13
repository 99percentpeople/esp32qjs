
static unsigned calls;
static uint8_t buffer[80],metadata[32];
void __real_nan_naf_txcb(void *value){
 assert(value==buffer);++calls;
 /* An app callback/notice has already returned, but the native tail remains. */
 assert(esp32_mquickjs_wifi_nan_sdk_service_quiesce(0)==ESP_ERR_TIMEOUT);
 assert(esp32_mquickjs_wifi_nan_sdk_service_resume(0)==ESP_ERR_INVALID_STATE);
}
