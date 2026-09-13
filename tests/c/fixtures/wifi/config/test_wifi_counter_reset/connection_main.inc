
int main(void){
    wifi_connection_note_submit();wifi_connection_note_failure();
    assert(s_wifi_connection_counters.attempts==1&&!s_wifi_connection_counters.reconnect_attempts&&s_wifi_connection_counters.submission_failures==1);
    wifi_connection_note_association();wifi_connection_note_submit();
    assert(s_wifi_connection_counters.reconnect_attempts==1&&s_wifi_connection_counters.associations==1);
    esp32_mquickjs_wifi_reset_connection_counters();
    assert(s_wifi_connection_counters.ever_associated&&!s_wifi_connection_counters.attempts&&!s_wifi_connection_counters.associations);
    wifi_connection_note_submit();assert(s_wifi_connection_counters.reconnect_attempts==1);
    s_wifi_connection_counters.attempts=UINT32_MAX;s_wifi_connection_counters.reconnect_attempts=UINT32_MAX;
    wifi_connection_note_submit();assert(s_wifi_connection_counters.attempts==UINT32_MAX&&s_wifi_connection_counters.reconnect_attempts==UINT32_MAX&&!locked);
}
