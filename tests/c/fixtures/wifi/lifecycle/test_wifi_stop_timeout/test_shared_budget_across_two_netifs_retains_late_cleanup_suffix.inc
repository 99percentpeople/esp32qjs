
int main(void) {
    esp_netif_t ap={.alive=true},sta={.alive=true};
    esp_netif_t *a=&ap,*s=&sta;int ae=0,se=0;
    assert(esp32_mquickjs_wifi_wait_begin(4)==ESP_OK);
    assert(esp32_mquickjs_wifi_netif_retire(&a,&ae)==ESP_OK && !a && ticks==3);
    assert(esp32_mquickjs_wifi_wait_remaining(1000)==1);
    assert(esp32_mquickjs_wifi_netif_retire(&s,&se)==ESP_ERR_TIMEOUT && ticks==4);
    assert(s && s_retire.netif==s && sta.detaches==1 && !se);
    uint32_t identity=s_retire.identity;int accepted=posts;
    esp32_mquickjs_wifi_wait_end();
    assert(esp32_mquickjs_wifi_wait_begin(4)==ESP_OK);
    assert(esp32_mquickjs_wifi_netif_retire(&s,&se)==ESP_OK && !s);
    assert(s_retire.identity==identity && sta.detaches==1 && posts==accepted && ticks==6);
    esp32_mquickjs_wifi_wait_end();
    assert(esp32_mquickjs_wifi_wait_remaining(1000)==1000);
}
