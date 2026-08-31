#include "esp32_mquickjs_wifi_csi_target.h"

#include <limits.h>
#include <string.h>

static void wifi_csi_layout_unknown(
    esp32_mquickjs_wifi_csi_metadata_t *metadata,
    esp32_mquickjs_wifi_csi_layout_schema_t schema,
    size_t frame_length)
{
    esp32_mquickjs_wifi_csi_layout_t *layout = &metadata->layout;

    memset(layout, 0, sizeof(*layout));
    layout->schema = schema;
    layout->byte_length = frame_length > UINT32_MAX
        ? UINT32_MAX : (uint32_t)frame_length;
    layout->segment_count = 1U;
    layout->segments[0].type = ESP32_MQUICKJS_WIFI_CSI_SEGMENT_UNKNOWN;
    layout->segments[0].length_bytes = layout->byte_length;
}

static esp32_mquickjs_wifi_csi_segment_t *wifi_csi_layout_add_segment(
    esp32_mquickjs_wifi_csi_layout_t *layout,
    esp32_mquickjs_wifi_csi_segment_type_t type,
    uint32_t iq_pair_count,
    int16_t first_start, int16_t first_end,
    bool second_range, int16_t second_start, int16_t second_end)
{
    esp32_mquickjs_wifi_csi_segment_t *segment;

    if (layout == NULL ||
        layout->segment_count >= ESP32_MQUICKJS_WIFI_CSI_MAX_SEGMENTS) {
        return NULL;
    }
    segment = &layout->segments[layout->segment_count++];
    memset(segment, 0, sizeof(*segment));
    segment->type = type;
    segment->iq_pair_count = iq_pair_count;
    segment->subcarrier_range_count = second_range ? 2U : 1U;
    segment->subcarrier_ranges[0].start = first_start;
    segment->subcarrier_ranges[0].end = first_end;
    if (second_range) {
        segment->subcarrier_ranges[1].start = second_start;
        segment->subcarrier_ranges[1].end = second_end;
    }
    layout->iq_pair_count += iq_pair_count;
    return segment;
}

static bool wifi_csi_layout_finalize(
    esp32_mquickjs_wifi_csi_layout_t *layout,
    size_t frame_length,
    esp32_mquickjs_wifi_csi_sample_encoding_t encoding,
    uint8_t sample_bits, uint8_t bytes_per_pair)
{
    size_t data_bytes;
    size_t offset = 0U;
    uint8_t index;

    if (layout == NULL || layout->segment_count == 0U ||
        layout->iq_pair_count == 0U || bytes_per_pair == 0U ||
        layout->iq_pair_count > SIZE_MAX / bytes_per_pair) {
        return false;
    }
    data_bytes = (size_t)layout->iq_pair_count * bytes_per_pair;
    if (frame_length < data_bytes || frame_length - data_bytes > 3U) {
        return false;
    }
    for (index = 0U; index < layout->segment_count; ++index) {
        esp32_mquickjs_wifi_csi_segment_t *segment = &layout->segments[index];

        segment->offset_bytes = (uint32_t)offset;
        segment->length_bytes = segment->iq_pair_count * bytes_per_pair;
        offset += segment->length_bytes;
    }
    layout->sample_encoding = encoding;
    layout->sample_bits = sample_bits;
    layout->byte_length = (uint32_t)frame_length;
    layout->trailing_padding_bytes = (uint16_t)(frame_length - data_bytes);
    layout->known = true;
    return true;
}

static void wifi_csi_segment_set_nulls(
    esp32_mquickjs_wifi_csi_segment_t *segment,
    const int16_t *values, uint8_t count)
{
    if (segment == NULL || values == NULL ||
        count > ESP32_MQUICKJS_WIFI_CSI_MAX_NULL_SUBCARRIERS) return;
    memcpy(segment->null_subcarriers, values,
           (size_t)count * sizeof(values[0]));
    segment->null_subcarrier_count = count;
}

void esp32_mquickjs_wifi_csi_target_build_legacy_layout(
    esp32_mquickjs_wifi_csi_metadata_t *metadata,
    const esp32_mquickjs_wifi_csi_capture_config_t *config,
    size_t frame_length)
{
    esp32_mquickjs_wifi_csi_layout_t *layout;
    const esp32_mquickjs_wifi_csi_legacy_config_t *capture;
    uint32_t ltf_pairs = 0U;
    int16_t first_start = 0;
    int16_t first_end = 0;
    int16_t second_start = 0;
    int16_t second_end = 0;
    bool second_range = false;

    if (metadata == NULL || config == NULL ||
        config->schema != ESP32_MQUICKJS_WIFI_CSI_SCHEMA_LEGACY) return;
    wifi_csi_layout_unknown(
        metadata, ESP32_MQUICKJS_WIFI_CSI_LAYOUT_SCHEMA_LEGACY,
        frame_length);
    layout = &metadata->layout;
    memset(layout->segments, 0, sizeof(layout->segments));
    layout->segment_count = 0U;
    layout->iq_pair_count = 0U;
    capture = &config->config.legacy;

    if (capture->lltf) {
        if (metadata->secondary == ESP32_MQUICKJS_WIFI_CSI_SECONDARY_BELOW) {
            (void)wifi_csi_layout_add_segment(
                layout, ESP32_MQUICKJS_WIFI_CSI_SEGMENT_LLTF, 64U,
                0, 63, false, 0, 0);
        } else if (metadata->secondary ==
                   ESP32_MQUICKJS_WIFI_CSI_SECONDARY_ABOVE) {
            (void)wifi_csi_layout_add_segment(
                layout, ESP32_MQUICKJS_WIFI_CSI_SEGMENT_LLTF, 64U,
                -64, -1, false, 0, 0);
        } else {
            (void)wifi_csi_layout_add_segment(
                layout, ESP32_MQUICKJS_WIFI_CSI_SEGMENT_LLTF, 64U,
                0, 31, true, -32, -1);
        }
    }
    if (metadata->phy == ESP32_MQUICKJS_WIFI_CSI_PHY_HT &&
        (capture->ht_ltf || capture->stbc_ht_ltf2)) {
        if (!metadata->bandwidth_available ||
            !metadata->stbc_available) goto unknown;
        if (metadata->secondary == ESP32_MQUICKJS_WIFI_CSI_SECONDARY_NONE) {
            if (metadata->bandwidth_mhz != 20U) goto unknown;
            ltf_pairs = 64U;
            first_start = 0;
            first_end = 31;
            second_range = true;
            second_start = -32;
            second_end = -1;
        } else if (metadata->bandwidth_mhz == 20U) {
            if (metadata->secondary == ESP32_MQUICKJS_WIFI_CSI_SECONDARY_BELOW) {
                ltf_pairs = metadata->stbc ? 63U : 64U;
                first_start = 0;
                first_end = metadata->stbc ? 62 : 63;
            } else {
                ltf_pairs = metadata->stbc ? 62U : 64U;
                first_start = metadata->stbc ? -62 : -64;
                first_end = -1;
            }
        } else if (metadata->bandwidth_mhz == 40U) {
            ltf_pairs = metadata->stbc ? 121U : 128U;
            first_start = 0;
            first_end = metadata->stbc ? 60 : 63;
            second_range = true;
            second_start = metadata->stbc ? -60 : -64;
            second_end = -1;
        } else {
            goto unknown;
        }
        if (capture->ht_ltf) {
            (void)wifi_csi_layout_add_segment(
                layout, ESP32_MQUICKJS_WIFI_CSI_SEGMENT_HT_LTF, ltf_pairs,
                first_start, first_end, second_range,
                second_start, second_end);
        }
        if (metadata->stbc && capture->stbc_ht_ltf2) {
            (void)wifi_csi_layout_add_segment(
                layout, ESP32_MQUICKJS_WIFI_CSI_SEGMENT_STBC_HT_LTF2,
                ltf_pairs, first_start, first_end, second_range,
                second_start, second_end);
        }
    } else if (metadata->phy != ESP32_MQUICKJS_WIFI_CSI_PHY_LEGACY) {
        goto unknown;
    }
    if (!wifi_csi_layout_finalize(
            layout, frame_length,
            ESP32_MQUICKJS_WIFI_CSI_SAMPLE_ENCODING_SIGNED_INT8,
            8U, 2U)) goto unknown;
    return;

unknown:
    wifi_csi_layout_unknown(
        metadata, ESP32_MQUICKJS_WIFI_CSI_LAYOUT_SCHEMA_LEGACY,
        frame_length);
}

static bool wifi_csi_he_finalize_detected(
    esp32_mquickjs_wifi_csi_layout_t *layout, size_t frame_length)
{
    struct candidate {
        esp32_mquickjs_wifi_csi_sample_encoding_t encoding;
        uint8_t bits;
        uint8_t bytes_per_pair;
    } candidates[] = {
        {ESP32_MQUICKJS_WIFI_CSI_SAMPLE_ENCODING_SIGNED_INT8, 8U, 2U},
        {ESP32_MQUICKJS_WIFI_CSI_SAMPLE_ENCODING_SIGNED_INT12_PACKED, 12U, 3U},
        {ESP32_MQUICKJS_WIFI_CSI_SAMPLE_ENCODING_SIGNED_INT12_LE, 12U, 4U},
    };
    int matched = -1;
    uint8_t index;

    for (index = 0U; index < sizeof(candidates) / sizeof(candidates[0]); ++index) {
        size_t bytes = (size_t)layout->iq_pair_count *
            candidates[index].bytes_per_pair;

        if (frame_length >= bytes && frame_length - bytes <= 3U) {
            if (matched >= 0) return false;
            matched = index;
        }
    }
    return matched >= 0 && wifi_csi_layout_finalize(
        layout, frame_length, candidates[matched].encoding,
        candidates[matched].bits, candidates[matched].bytes_per_pair);
}

void esp32_mquickjs_wifi_csi_target_build_he_layout(
    esp32_mquickjs_wifi_csi_metadata_t *metadata,
    const esp32_mquickjs_wifi_csi_capture_config_t *config,
    size_t frame_length)
{
    esp32_mquickjs_wifi_csi_layout_t *layout;
    const esp32_mquickjs_wifi_csi_he_config_t *capture;
    esp32_mquickjs_wifi_csi_segment_t *segment = NULL;
    int16_t null_one[] = {0};
    int16_t null_three[] = {-1, 0, 1};
    uint32_t pairs;
    int16_t first_start;
    int16_t first_end;
    int16_t second_start = 0;
    int16_t second_end = 0;
    bool second_range = false;
    bool stbc;

    if (metadata == NULL || config == NULL ||
        config->schema != ESP32_MQUICKJS_WIFI_CSI_SCHEMA_HE) return;
    wifi_csi_layout_unknown(
        metadata, ESP32_MQUICKJS_WIFI_CSI_LAYOUT_SCHEMA_HE, frame_length);
    layout = &metadata->layout;
    memset(layout->segments, 0, sizeof(layout->segments));
    layout->segment_count = 0U;
    layout->iq_pair_count = 0U;
    capture = &config->config.he;

    if (metadata->phy == ESP32_MQUICKJS_WIFI_CSI_PHY_LEGACY ||
        capture->force_legacy_ltf) {
        if (!capture->enable_legacy) goto unknown;
        if (metadata->secondary == ESP32_MQUICKJS_WIFI_CSI_SECONDARY_BELOW) {
            segment = wifi_csi_layout_add_segment(
                layout, ESP32_MQUICKJS_WIFI_CSI_SEGMENT_LLTF, 53U,
                0, 52, false, 0, 0);
        } else if (metadata->secondary ==
                   ESP32_MQUICKJS_WIFI_CSI_SECONDARY_ABOVE) {
            segment = wifi_csi_layout_add_segment(
                layout, ESP32_MQUICKJS_WIFI_CSI_SEGMENT_LLTF, 53U,
                -53, -1, false, 0, 0);
        } else {
            segment = wifi_csi_layout_add_segment(
                layout, ESP32_MQUICKJS_WIFI_CSI_SEGMENT_LLTF, 53U,
                0, 26, true, -26, -1);
        }
        wifi_csi_segment_set_nulls(segment, null_one, 1U);
    } else if (metadata->phy == ESP32_MQUICKJS_WIFI_CSI_PHY_HT ||
               metadata->phy == ESP32_MQUICKJS_WIFI_CSI_PHY_VHT) {
        if (!metadata->bandwidth_available || !metadata->stbc_available) {
            goto unknown;
        }
        stbc = metadata->stbc;
        if (metadata->bandwidth_mhz == 20U) {
            pairs = 57U;
            if (metadata->secondary == ESP32_MQUICKJS_WIFI_CSI_SECONDARY_BELOW) {
                first_start = 0;
                first_end = 56;
            } else if (metadata->secondary ==
                       ESP32_MQUICKJS_WIFI_CSI_SECONDARY_ABOVE) {
                first_start = -57;
                first_end = -1;
            } else {
                first_start = 0;
                first_end = 28;
                second_range = true;
                second_start = -28;
                second_end = -1;
            }
        } else if (metadata->bandwidth_mhz == 40U &&
                   metadata->secondary !=
                       ESP32_MQUICKJS_WIFI_CSI_SECONDARY_NONE) {
            pairs = 117U;
            first_start = 0;
            first_end = 58;
            second_range = true;
            second_start = -58;
            second_end = -1;
        } else {
            goto unknown;
        }
        segment = wifi_csi_layout_add_segment(
            layout,
            metadata->phy == ESP32_MQUICKJS_WIFI_CSI_PHY_VHT
                ? ESP32_MQUICKJS_WIFI_CSI_SEGMENT_VHT_LTF
                : ESP32_MQUICKJS_WIFI_CSI_SEGMENT_HT_LTF,
            pairs, first_start, first_end, second_range,
            second_start, second_end);
        if (metadata->secondary == ESP32_MQUICKJS_WIFI_CSI_SECONDARY_NONE) {
            wifi_csi_segment_set_nulls(segment, null_one, 1U);
        } else if (metadata->bandwidth_mhz == 40U) {
            wifi_csi_segment_set_nulls(segment, null_three, 3U);
        }
        if (stbc) {
            segment = wifi_csi_layout_add_segment(
                layout, ESP32_MQUICKJS_WIFI_CSI_SEGMENT_STBC_HT_LTF2,
                pairs, first_start, first_end, second_range,
                second_start, second_end);
            if (metadata->secondary == ESP32_MQUICKJS_WIFI_CSI_SECONDARY_NONE) {
                wifi_csi_segment_set_nulls(segment, null_one, 1U);
            } else if (metadata->bandwidth_mhz == 40U) {
                wifi_csi_segment_set_nulls(segment, null_three, 3U);
            }
        }
    } else if (metadata->phy == ESP32_MQUICKJS_WIFI_CSI_PHY_HE_SU) {
        if (!metadata->stbc_available || !metadata->bandwidth_available ||
            metadata->bandwidth_mhz != 20U) goto unknown;
        segment = wifi_csi_layout_add_segment(
            layout,
            metadata->stbc && capture->he_stbc_ltf ==
                ESP32_MQUICKJS_WIFI_CSI_HE_STBC_SECOND
                ? ESP32_MQUICKJS_WIFI_CSI_SEGMENT_HE_LTF2
                : metadata->stbc && capture->he_stbc_ltf ==
                      ESP32_MQUICKJS_WIFI_CSI_HE_STBC_ALTERNATE
                    ? ESP32_MQUICKJS_WIFI_CSI_SEGMENT_MIXED
                    : ESP32_MQUICKJS_WIFI_CSI_SEGMENT_HE_LTF1,
            245U, 0, 122, true, -122, -1);
        wifi_csi_segment_set_nulls(segment, null_three, 3U);
    } else {
        goto unknown;
    }
    if (!wifi_csi_he_finalize_detected(layout, frame_length)) goto unknown;
    return;

unknown:
    wifi_csi_layout_unknown(
        metadata, ESP32_MQUICKJS_WIFI_CSI_LAYOUT_SCHEMA_HE, frame_length);
}
