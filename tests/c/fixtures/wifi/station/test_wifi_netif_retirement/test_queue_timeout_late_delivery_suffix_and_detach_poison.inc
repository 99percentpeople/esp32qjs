
int main(void) {
    esp_netif_t sta={.alive=true},ap={.alive=true};
    esp_netif_t *s=&sta,*a=&ap;int se=0,ae=0;
    assert(esp32_mquickjs_wifi_netif_retire(NULL,&se)==ESP_ERR_INVALID_ARG);
    register_error=17;
    assert(esp32_mquickjs_wifi_netif_retire(&s,&se)==17 && s && !se && !posts);
    register_error=0;post_error=19;
    assert(esp32_mquickjs_wifi_netif_retire(&s,&se)==19 && s && !se && !sta.stops);
    assert(s_retire.phase==NETIF_IDLE && !s_retire.netif);
    post_error=0;auto_dispatch=false;
    assert(esp32_mquickjs_wifi_netif_retire(&s,&se)==ESP_ERR_TIMEOUT);
    uint32_t first=s_retire.identity;int accepted=posts;
    assert(s && !se && s_retire.phase==NETIF_DETACH_QUEUED && !sta.detaches);
    assert(esp32_mquickjs_wifi_netif_retire(&a,&ae)==ESP_ERR_INVALID_STATE);
    assert(a && !ae && !ap.stops && posts==accepted);
    /* Retry never re-posts a job already admitted, even across runtime detach. */
    assert(esp32_mquickjs_wifi_netif_retire(&s,&se)==ESP_ERR_TIMEOUT && posts==accepted);
    dispatch();assert(sta.detaches==1 && !sta.ips && s_retire.phase==NETIF_DETACHED);
    post_error=23;
    assert(esp32_mquickjs_wifi_netif_retire(&s,&se)==23 && s && !se);
    assert(s_retire.phase==NETIF_DETACHED && sta.detaches==1);
    post_error=0;
    assert(esp32_mquickjs_wifi_netif_retire(&s,&se)==ESP_ERR_TIMEOUT);
    assert(s_retire.phase==NETIF_FENCE_QUEUED && sta.alive);
    dispatch();assert(sta.ips==1 && sta.alive);
    dispatch();assert(s_retire.phase==NETIF_DONE && sta.alive);
    assert(esp32_mquickjs_wifi_netif_retire(&s,&se)==ESP_OK && !s && !sta.alive);
    assert(esp32_mquickjs_wifi_netif_retire(&s,&se)==ESP_OK);
    /* A duplicate/stale completion cannot detach the next slot occupant. */
    assert(esp32_mquickjs_wifi_netif_retire(&a,&ae)==ESP_ERR_TIMEOUT);
    handler(NULL,ESP32QJS_WIFI_NETIF_EVENT,NETIF_DETACH_QUEUED,&first);
    handler(NULL,ESP32QJS_WIFI_NETIF_EVENT,NETIF_FENCE_QUEUED,&first);
    assert(!ap.detaches && s_retire.phase==NETIF_DETACH_QUEUED);
    auto_dispatch=true;
    assert(esp32_mquickjs_wifi_netif_retire(&a,&ae)==ESP_OK && !a && !ae && !ap.alive);
    assert(sta.detaches==1 && ap.detaches==1 && registrations==2);
    /* SDK error is different from admission/timeout: permanently poison owner. */
    esp_netif_t failed={.alive=true};esp_netif_t *f=&failed;int fe=0;
    detach_error=29;
    assert(esp32_mquickjs_wifi_netif_retire(&f,&fe)==29 && fe==29 && f && failed.alive);
    accepted=posts;detach_error=0;
    assert(esp32_mquickjs_wifi_netif_retire(&f,&fe)==29 && posts==accepted && failed.detaches==1);
    while(head!=tail)dispatch();
    /* Identity exhaustion is explicit and never wraps/reuses an old event. */
    esp_netif_t last={.alive=true};esp_netif_t *l=&last;int le=0;
    s_retire.identity=UINT32_MAX;
    assert(esp32_mquickjs_wifi_netif_retire(&l,&le)==ESP_ERR_INVALID_STATE);
    assert(l && !le && !last.detaches && posts==accepted);
    return 0;
}
