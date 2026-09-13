#pragma once

/* Requires esp32_mquickjs_set_property_ref from esp32_mquickjs_core.h.
 * object_ref must refer to a live GC root. Each expression is evaluated once;
 * the caller supplies context and cleanup label explicitly. This macro neither
 * pops roots nor clears exceptions, so the caller retains cleanup ownership. */
#define ESP32_MQUICKJS_SET_OR_GOTO(ctx, object_ref, name, value, cleanup) \
    do { \
        if (!esp32_mquickjs_set_property_ref((ctx), (object_ref), (name), (value))) \
            goto cleanup; \
    } while (0)
