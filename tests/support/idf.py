"""Prerequisites for fixtures that consume an external ESP-IDF source tree."""
import os
from pathlib import Path
import unittest


def require_idf():
    value = os.environ.get("IDF_PATH")
    if not value or not (Path(value) / "components/esp_wifi/include/esp_wifi.h").is_file():
        if os.environ.get("ESP32QJS_REQUIRE_SDK") == "1":
            raise RuntimeError("SDK validation requires an existing IDF_PATH")
        raise unittest.SkipTest("ESP-IDF source unavailable; set IDF_PATH")
    return Path(value)


def require_target_tool(target, name):
    from build_tools.toolchains import find_target_tool
    tool = find_target_tool(target, name)
    if tool:
        return tool
    message = f"ESP-IDF target tool unavailable: {target}/{name}"
    if os.environ.get("ESP32QJS_REQUIRE_SDK") == "1":
        raise RuntimeError(message)
    raise unittest.SkipTest(message)
