
int main(void) {
    esp32_mquickjs_future_driver_state_t *s=calloc(1,sizeof(*s));
    memset(s->connect_config.sta.password, 0xa5, 64);
    wifi_future_destroy(s);
}
