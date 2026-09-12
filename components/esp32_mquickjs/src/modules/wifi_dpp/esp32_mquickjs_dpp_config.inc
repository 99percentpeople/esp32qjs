/* Validate complete material before deciding whether an installed row matches.
 * Configuration equality includes every key byte and expiry, not just JWS. */
static bool esp32qjs_dpp_config_valid(const esp_dpp_config_data_t *row)
{
    return row && row->akm >= ESP_DPP_AKM_DPP && row->akm <= ESP_DPP_AKM_PSK_SAE_DPP &&
        dpp_akm_dpp((enum dpp_akm)row->akm) && row->connector_len &&
        row->connector_len < sizeof(row->connector) && !row->connector[row->connector_len] &&
        !memchr(row->connector, 0, row->connector_len) &&
        row->net_access_key_len && row->net_access_key_len <= sizeof(row->net_access_key) &&
        row->c_sign_key_len && row->c_sign_key_len <= sizeof(row->c_sign_key);
}

static bool esp_dpp_stored_conf_matches_row(struct dpp_config_store *dc,
    const esp_dpp_config_data_t *row)
{
    if (!dc || !esp32qjs_dpp_config_valid(row)) return false;
    struct dpp_conf *c = dc->conf;
    if (!c || c->akm != (enum dpp_akm)row->akm || c->curr_chan != row->curr_chan ||
        c->net_access_key_expiry != row->net_access_key_expiry || !c->connector ||
        !c->net_access_key || !c->c_sign_key) return false;
    return os_strlen(c->connector) == row->connector_len &&
        !os_memcmp(c->connector, row->connector, row->connector_len) &&
        wpabuf_len(c->net_access_key) == row->net_access_key_len &&
        !os_memcmp(wpabuf_head(c->net_access_key), row->net_access_key, row->net_access_key_len) &&
        wpabuf_len(c->c_sign_key) == row->c_sign_key_len &&
        !os_memcmp(wpabuf_head(c->c_sign_key), row->c_sign_key, row->c_sign_key_len);
}
