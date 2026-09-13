
static esp32_mquickjs_wifi_eap_profile_t *make_profile(uint8_t byte){
    esp32_mquickjs_wifi_eap_input_t in={.policy={.methods=ESP_EAP_TYPE_PEAP}};
    in.fields[ESP32_MQUICKJS_WIFI_EAP_USERNAME]=(esp32_mquickjs_wifi_eap_span_t){&byte,1};
    esp32_mquickjs_wifi_eap_profile_t *profile=NULL;
    assert(esp32_mquickjs_wifi_eap_profile_create(&in,&profile)==0);return profile;
}
static uint64_t revision(void){esp32_mquickjs_wifi_eap_config_status_t s;esp32_mquickjs_wifi_eap_config_status(&s);return s.revision;}
static esp32_mquickjs_wifi_eap_config_token_t raced;
static esp32_mquickjs_wifi_eap_profile_t *raced_profile;
static void begin_between_query_and_commit(void){
    assert(esp32_mquickjs_wifi_eap_config_begin(revision(),ESP32_MQUICKJS_WIFI_EAP_CONFIG_ENABLE,&raced,&raced_profile)==0);
}
int main(void){
    esp32_mquickjs_wifi_eap_profile_t *a=make_profile(1),*b=make_profile(2),*borrow=NULL;
    esp32_mquickjs_wifi_eap_config_status_t status;
    assert(esp32_mquickjs_wifi_eap_config_replace(0,a)==ESP_ERR_INVALID_STATE && a->refs==1);
    assert(esp32_mquickjs_wifi_eap_config_open()==0);uint64_t captured=revision();
    a->refs=UINT32_MAX;
    assert(esp32_mquickjs_wifi_eap_config_replace(captured,a)==ESP_ERR_NO_MEM && revision()==captured);
    a->refs=1;
    assert(esp32_mquickjs_wifi_eap_config_replace(captured,a)==0 && a->refs==2);
    assert(esp32_mquickjs_wifi_eap_config_replace(captured,b)==ESP_ERR_INVALID_STATE && b->refs==1 && a->refs==2);
    captured=revision();
    /* A reentrant inner commit wins; the outer capture's stale commit cannot
     * replace it or release its independent reference. */
    assert(esp32_mquickjs_wifi_eap_config_replace(captured,b)==0 && a->refs==1 && b->refs==2);
    assert(esp32_mquickjs_wifi_eap_config_replace(captured,a)==ESP_ERR_INVALID_STATE && b->refs==2);
    esp32_mquickjs_wifi_eap_config_token_t token={0},stale;
    b->refs=UINT32_MAX;
    assert(esp32_mquickjs_wifi_eap_config_begin(revision(),ESP32_MQUICKJS_WIFI_EAP_CONFIG_ENABLE,&token,&borrow)==ESP_ERR_NO_MEM && !token.identity && !borrow);
    b->refs=2;
    assert(esp32_mquickjs_wifi_eap_config_begin(revision(),ESP32_MQUICKJS_WIFI_EAP_CONFIG_ENABLE,&token,&borrow)==0 && borrow==b && b->refs==3);
    stale=token;assert(esp32_mquickjs_wifi_eap_config_replace(revision(),a)==ESP_ERR_INVALID_STATE);
    binding=91;assert(esp32_mquickjs_wifi_eap_config_finish(&token,false)==0 && !token.identity && b->refs==2);
    assert(esp32_mquickjs_wifi_eap_config_replace(revision(),a)==ESP_ERR_INVALID_STATE);
    assert(esp32_mquickjs_wifi_eap_config_begin(revision(),ESP32_MQUICKJS_WIFI_EAP_CONFIG_CLEAR,&token,&borrow)==0);
    assert(esp32_mquickjs_wifi_eap_config_finish(&stale,false)==ESP_ERR_INVALID_STATE && b->refs==3);
    assert(esp32_mquickjs_wifi_eap_config_finish(&token,true)==ESP_ERR_INVALID_STATE && !token.identity && b->refs==2);
    /* Failed SDK retirement frees only the completed control's own reference. */
    esp32_mquickjs_wifi_eap_config_status(&status);assert(status.configured && !status.busy);
    binding=0;
    query_hook=begin_between_query_and_commit;
    assert(esp32_mquickjs_wifi_eap_config_replace(revision(),a)==ESP_ERR_INVALID_STATE && raced.identity && raced_profile==b);
    assert(esp32_mquickjs_wifi_eap_config_finish(&raced,false)==0 && b->refs==2);
    captured=revision();
    assert(esp32_mquickjs_wifi_eap_config_begin(captured,ESP32_MQUICKJS_WIFI_EAP_CONFIG_DISABLE,&token,&borrow)==0);
    /* Teardown invalidates new captures, but the exact old control may finish. */
    esp32_mquickjs_wifi_eap_config_begin_close();uint64_t closed_revision=revision();assert(closed_revision>captured);
    esp32_mquickjs_wifi_eap_config_begin_close();assert(revision()==closed_revision);
    assert(!esp32_mquickjs_wifi_eap_config_finish_close());
    assert(esp32_mquickjs_wifi_eap_config_replace(captured,a)==ESP_ERR_INVALID_STATE);
    binding=91;assert(esp32_mquickjs_wifi_eap_config_finish(&token,false)==0 && b->refs==2);
    assert(!esp32_mquickjs_wifi_eap_config_finish_close() && b->refs==2);
    assert(esp32_mquickjs_wifi_eap_config_open()==ESP_ERR_INVALID_STATE);
    binding=0;assert(esp32_mquickjs_wifi_eap_config_finish_close() && b->refs==1);
    assert(esp32_mquickjs_wifi_eap_config_open()==0 && revision()>closed_revision);
    assert(esp32_mquickjs_wifi_eap_config_replace(captured,a)==ESP_ERR_INVALID_STATE);
    assert(esp32_mquickjs_wifi_eap_config_replace(revision(),a)==0);
    /* Same identity with altered revision/action cannot end the live control. */
    assert(esp32_mquickjs_wifi_eap_config_begin(revision(),ESP32_MQUICKJS_WIFI_EAP_CONFIG_CLEAR,&token,&borrow)==0);
    stale=token;stale.revision++;assert(esp32_mquickjs_wifi_eap_config_finish(&stale,true)==ESP_ERR_INVALID_STATE);
    stale=token;stale.action=ESP32_MQUICKJS_WIFI_EAP_CONFIG_ENABLE;assert(esp32_mquickjs_wifi_eap_config_finish(&stale,true)==ESP_ERR_INVALID_STATE);
    assert(esp32_mquickjs_wifi_eap_config_finish(&token,true)==0 && a->refs==1);
    /* Exhaustion never wraps; shutdown still releases a configured owner. */
    assert(esp32_mquickjs_wifi_eap_config_replace(revision(),a)==0);
    s_eap_config.next_identity=UINT64_MAX;
    assert(esp32_mquickjs_wifi_eap_config_begin(revision(),ESP32_MQUICKJS_WIFI_EAP_CONFIG_ENABLE,&token,&borrow)==ESP_ERR_INVALID_STATE && !borrow);
    s_eap_config.revision=UINT64_MAX;
    assert(esp32_mquickjs_wifi_eap_config_replace(UINT64_MAX,b)==ESP_ERR_INVALID_STATE);
    esp32_mquickjs_wifi_eap_config_begin_close();assert(esp32_mquickjs_wifi_eap_config_finish_close() && a->refs==1);
    assert(esp32_mquickjs_wifi_eap_config_open()==ESP_ERR_INVALID_STATE);
    esp32_mquickjs_wifi_eap_config_status(&status);assert(status.closing && status.revision_exhausted && status.identity_exhausted);
    esp32_mquickjs_wifi_eap_profile_release(a);esp32_mquickjs_wifi_eap_profile_release(b);
    assert(!s_eap_profile_counts.profiles && !s_eap_profile_counts.reserved_bytes && !critical);
    return 0;
}
