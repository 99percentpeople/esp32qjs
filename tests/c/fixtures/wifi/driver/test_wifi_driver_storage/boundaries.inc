
static wifi_storage_t native_storage;
static void (*storage_hook)(void);
static int esp_wifi_set_storage(wifi_storage_t requested) {
    int err=sdk_step(true);native_storage=requested; /* Partial mutation even on error. */
    if(storage_hook)storage_hook();return err;
}
