#include <limits.h>
/* Fixed-SDK internal bridge: structured input cannot add command parameters.
 * URI info permits spaces, but never control characters or the ';' delimiter.
 * No callback contract is added; the SDK continues using Wi-Fi events. */
int esp32qjs_dpp_bootstrap_generate(struct dpp_global *dpp, const char *channels,
    const u8 mac[ETH_ALEN], const u8 *private_key, size_t private_key_length, const char *info)
{
    struct dpp_bootstrap_info *bi;
    u8 key_copy[256] = {0};
    int ret = -1;
    if (!dpp || !channels || !mac || (info && !dpp_uri_valid_info(info)) ||
        (!!private_key != !!private_key_length) || private_key_length > sizeof(key_copy)) return -1;
    bi = os_zalloc(sizeof(*bi));
    if (!bi) return -1;
    bi->type = DPP_BOOTSTRAP_QR_CODE;
    if (private_key) os_memcpy(key_copy, private_key, private_key_length);
    bi->chan = os_strdup(channels);
    if (!bi->chan) goto fail;
    os_memcpy(bi->mac_addr, mac, ETH_ALEN);
    if (dpp_keygen(bi, NULL, private_key ? key_copy : NULL, private_key_length) < 0 ||
        dpp_parse_uri_chan_list(bi, bi->chan) < 0 ||
        dpp_parse_uri_info(bi, info) < 0 || dpp_gen_uri(bi) < 0) goto fail;
    unsigned int identity = dpp_next_id(dpp);
    if (!identity || identity > INT_MAX) goto fail;
    bi->id = identity;
    dl_list_add(&dpp->bootstrap, &bi->list);
    ret = bi->id;
    bi = NULL;
fail:
    forced_memzero(key_copy, sizeof(key_copy));
    dpp_bootstrap_info_free(bi);
    return ret;
}
