"""Assemble production connection observation storage/helpers; no execution on import."""

from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]


def connection_counter_code(*, converter=False):
    source = (ROOT / "components/esp32_mquickjs/src/modules/wifi/esp32_mquickjs_wifi.c").read_text()
    # SDK lock boundary is supplied by the caller. No replacement counter logic.
    start = source.index("static portMUX_TYPE s_wifi_connection_counters_lock")
    end = source.index("static void wifi_connection_counter_add", start)
    code = source[start:end]
    names = ["wifi_connection_counter_add", "wifi_connection_note_submit", "wifi_connection_note_failure",
             "wifi_connection_note_association", "esp32_mquickjs_wifi_reset_connection_counters"]
    if converter:
        names.append("wifi_connection_counters_to_js")
    for name in names:
        match = re.search(r"(?:static )?[\w *]+\b" + name + r"\([^;{}]*\)\n\{", source)
        code += source[match.start():source.index("\n}\n", match.start()) + 3]
    return code
