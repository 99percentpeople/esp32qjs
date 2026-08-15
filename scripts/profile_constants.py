"""Validation and C include generation for immutable hardware-profile constants."""

from __future__ import annotations

import json
import re
from pathlib import Path
from typing import Any


ROOT_DIR = Path(__file__).resolve().parent.parent
CATALOG_PATH = ROOT_DIR / "configs" / "profile-constants.json"
KEY_RE = re.compile(r"^[A-Z][A-Z0-9_]{0,63}$")
RESERVED_PREFIX = "ESP32QJS_"

LEGACY_WIRING_KEYS = {
    ("led", "pin"): "ESP32QJS_LED_PIN",
    ("led", "activeLow"): "ESP32QJS_LED_ACTIVE_LOW",
    ("i2c", "sda"): "ESP32QJS_I2C_SDA",
    ("i2c", "scl"): "ESP32QJS_I2C_SCL",
    ("spi", "sclk"): "ESP32QJS_SPI_SCLK",
    ("spi", "mosi"): "ESP32QJS_SPI_MOSI",
    ("spi", "miso"): "ESP32QJS_SPI_MISO",
    ("spi", "cs"): "ESP32QJS_SPI_CS",
    ("uart", "tx"): "ESP32QJS_UART_TX",
    ("uart", "rx"): "ESP32QJS_UART_RX",
}


def load_catalog(path: Path = CATALOG_PATH) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict) or value.get("version") != 1:
        raise ValueError("profile constant catalog has an unsupported version")
    definitions = value.get("definitions")
    mcus = value.get("mcus")
    limits = value.get("limits")
    if not isinstance(definitions, list) or not isinstance(mcus, dict) or not isinstance(limits, dict):
        raise ValueError("profile constant catalog is incomplete")
    seen: set[str] = set()
    for definition in definitions:
        if not isinstance(definition, dict) or not isinstance(definition.get("key"), str):
            raise ValueError("profile constant catalog contains an invalid definition")
        key = definition["key"]
        if not KEY_RE.fullmatch(key) or key in seen:
            raise ValueError(f"profile constant catalog contains an invalid or duplicate key: {key}")
        seen.add(key)
    return value


def legacy_wiring_constants(value: dict[str, Any]) -> dict[str, Any]:
    constants: dict[str, Any] = {}
    for (group, field), key in LEGACY_WIRING_KEYS.items():
        entry = value.get(group)
        if not isinstance(entry, dict) or field not in entry:
            continue
        candidate = entry[field]
        if field == "pin" and candidate == -1:
            continue
        if group == "i2c" and candidate == -1:
            continue
        constants[key] = candidate
    return constants


def constants_from_document(value: Any) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise ValueError("hardware constants must be a JSON object")
    if "constants" in value:
        constants = value.get("constants")
        if not isinstance(constants, dict):
            raise ValueError("hardware constants document must contain an object named constants")
        return constants
    if set(value).issubset({"led", "i2c", "spi", "uart"}):
        return legacy_wiring_constants(value)
    return value


def validate_constants(
    raw: Any,
    mcu: str,
    catalog: dict[str, Any] | None = None,
) -> tuple[dict[str, str | int | bool], list[str]]:
    catalog = catalog or load_catalog()
    constants = constants_from_document(raw)
    limits = catalog["limits"]
    max_entries = int(limits["maxEntries"])
    max_document_bytes = int(limits["maxDocumentBytes"])
    max_string_bytes = int(limits["maxStringBytes"])
    if len(constants) > max_entries:
        raise ValueError(f"hardware constants exceed the {max_entries}-entry limit")
    if len(json.dumps(constants, ensure_ascii=False, separators=(",", ":")).encode("utf-8")) > max_document_bytes:
        raise ValueError(f"hardware constants exceed the {max_document_bytes}-byte limit")

    mcu_profile = catalog["mcus"].get(mcu)
    if not isinstance(mcu_profile, dict):
        raise ValueError(f"hardware constants do not support MCU {mcu}")
    gpio_max = int(mcu_profile["gpioMax"])
    reserved = set(int(pin) for pin in mcu_profile.get("reservedGpios", []))
    warning = set(int(pin) for pin in mcu_profile.get("warningGpios", []))
    definitions = {entry["key"]: entry for entry in catalog["definitions"]}
    result: dict[str, str | int | bool] = {}
    warnings: list[str] = []

    for key in sorted(constants):
        if not isinstance(key, str) or not KEY_RE.fullmatch(key):
            raise ValueError(f"invalid hardware constant key: {key}")
        definition = definitions.get(key)
        if key.startswith(RESERVED_PREFIX) and definition is None:
            raise ValueError(f"unknown reserved hardware constant: {key}")
        value = constants[key]
        if definition is None:
            if isinstance(value, bool):
                result[key] = value
            elif isinstance(value, int) and not isinstance(value, bool) and -(2**31) <= value < 2**31:
                result[key] = value
            elif isinstance(value, str) and len(value.encode("utf-8")) <= max_string_bytes:
                result[key] = value
            else:
                raise ValueError(f"custom hardware constant {key} must be a bounded string, boolean, or 32-bit integer")
            continue

        value_type = definition.get("type")
        if value_type == "boolean":
            if not isinstance(value, bool):
                raise ValueError(f"{key} must be boolean")
        elif value_type in {"integer", "gpio"}:
            if not isinstance(value, int) or isinstance(value, bool):
                raise ValueError(f"{key} must be an integer")
            minimum = int(definition.get("minimum", -1 if definition.get("allowMinusOne") else 0))
            maximum = int(definition.get("maximum", gpio_max if value_type == "gpio" else 2**31 - 1))
            if value < minimum or value > maximum:
                raise ValueError(f"{key} must be between {minimum} and {maximum}")
            if value_type == "gpio" and value >= 0:
                if value in reserved:
                    raise ValueError(f"{key} uses GPIO {value}, reserved for flash or PSRAM")
                if value in warning:
                    warnings.append(f"{key} uses boot or USB-related GPIO {value}")
        else:
            raise ValueError(f"{key} has an unsupported catalog type")
        result[key] = value
    return result, list(dict.fromkeys(warnings))


def render_c_include(constants: dict[str, str | int | bool]) -> str:
    def c_string(value: str) -> str:
        escaped: list[str] = []
        for byte in value.encode("utf-8"):
            if 0x20 <= byte <= 0x7E and byte not in (ord('"'), ord("\\")):
                escaped.append(chr(byte))
            elif byte == ord('"'):
                escaped.append('\\"')
            elif byte == ord("\\"):
                escaped.append("\\\\")
            else:
                escaped.append(f"\\{byte:03o}")
        return '"' + "".join(escaped) + '"'

    lines = ["/* Generated hardware-profile constants; do not edit. */"]
    for key in sorted(constants):
        value = constants[key]
        key_literal = c_string(key)
        if isinstance(value, bool):
            lines.append(
                f"ESP32_MQUICKJS_PROFILE_BOOL({key_literal}, {'true' if value else 'false'}),"
            )
        elif isinstance(value, int):
            lines.append(f"ESP32_MQUICKJS_PROFILE_INT({key_literal}, {value}),")
        else:
            lines.append(
                f"ESP32_MQUICKJS_PROFILE_STRING({key_literal}, {c_string(value)}),"
            )
    return "\n".join(lines) + "\n"
