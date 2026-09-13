"""Prerequisites for fixtures that consume an external ESP-IDF source tree."""
import os
from pathlib import Path
import unittest


def require_idf():
    value = os.environ.get("IDF_PATH")
    if not value or not (Path(value) / "components/esp_wifi/include/esp_wifi.h").is_file():
        raise unittest.SkipTest("ESP-IDF source unavailable; set IDF_PATH")
    return Path(value)
