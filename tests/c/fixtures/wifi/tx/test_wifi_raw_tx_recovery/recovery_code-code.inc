static unsigned s_policies;
        typedef struct {bool known,uncertain;unsigned generation,write_identity;} esp32_mquickjs_wifi_tx_rate_record_t;
        static struct {esp32_mquickjs_wifi_tx_rate_record_t records[2];} s_tx_rates;
        static bool esp32_mquickjs_wifi_tx_rate_valid(const int *rate) {return *rate==11;}