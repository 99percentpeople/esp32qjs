#!/usr/bin/env python3
"""Generate and validate the ESP32QJS native callable API manifest."""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
STDLIB_PATH = ROOT / "components/esp32_mquickjs/src/core/mqjs_stdlib_esp32.c"
TYPES_PATH = ROOT / "types/esp32qjs-c-api.d.ts"
DOCS_PATH = ROOT / "docs/c-api.md"
KCONFIG_PATH = ROOT / "components/esp32_mquickjs/Kconfig.projbuild"
MANIFEST_PATH = ROOT / "api-manifest.json"

CORE = "core"
FEATURES = {
    "fs": "CONFIG_ESP32_MQUICKJS_FEATURE_FS",
    "nvs": "CONFIG_ESP32_MQUICKJS_FEATURE_NVS",
    "gpio": "CONFIG_ESP32_MQUICKJS_FEATURE_GPIO",
    "ledc": "CONFIG_ESP32_MQUICKJS_FEATURE_LEDC",
    "adc": "CONFIG_ESP32_MQUICKJS_FEATURE_ADC",
    "dac": "CONFIG_ESP32_MQUICKJS_FEATURE_DAC",
    "i2c": "CONFIG_ESP32_MQUICKJS_FEATURE_I2C",
    "spi": "CONFIG_ESP32_MQUICKJS_FEATURE_SPI",
    "uart": "CONFIG_ESP32_MQUICKJS_FEATURE_UART",
    "rmt": "CONFIG_ESP32_MQUICKJS_FEATURE_RMT",
    "i2s": "CONFIG_ESP32_MQUICKJS_FEATURE_I2S",
    "camera": "CONFIG_ESP32_MQUICKJS_FEATURE_CAMERA",
    "usbSerial": "CONFIG_ESP32_MQUICKJS_FEATURE_USB_SERIAL",
    "rpc": "CONFIG_ESP32_MQUICKJS_FEATURE_RPC",
    "socket": "CONFIG_ESP32_MQUICKJS_FEATURE_SOCKET",
    "websocket": "CONFIG_ESP32_MQUICKJS_FEATURE_WEBSOCKET",
    "wifi": "CONFIG_ESP32_MQUICKJS_FEATURE_WIFI",
    "wifiCsi": "CONFIG_ESP32_MQUICKJS_FEATURE_WIFI_CSI",
    "espNow": "CONFIG_ESP32_MQUICKJS_FEATURE_ESPNOW",
    "ble": "CONFIG_ESP32_MQUICKJS_FEATURE_BLE",
    "net": "CONFIG_ESP32_MQUICKJS_FEATURE_NET",
    "http": "CONFIG_ESP32_MQUICKJS_FEATURE_HTTP",
    "httpServer": "CONFIG_ESP32_MQUICKJS_FEATURE_HTTP_SERVER",
    "runtimeLogs": "CONFIG_ESP32_MQUICKJS_FEATURE_RUNTIME_LOGS",
    "bitmap": "CONFIG_ESP32_MQUICKJS_FEATURE_BITMAP",
}

# table: (surface, TypeScript declaration, feature, documentation owner token)
SURFACES = {
    "js_headers_proto": ("Headers.prototype", "Headers", CORE, "Headers"),
    "js_request_proto": ("Request.prototype", "Request", CORE, "Request"),
    "js_response": ("Response", "Response", CORE, "Response"),
    "js_response_proto": ("Response.prototype", "Response", CORE, "Response"),
    "js_future": ("Future", "FutureFactory", CORE, "Future"),
    "js_future_proto": ("Future.prototype", "Future", CORE, "Future"),
    "js_event_queue_proto": ("EventQueue.prototype", "EventQueue", CORE, "EventQueue"),
    "js_stream_proto": ("Stream.prototype", "Stream", CORE, "Stream"),
    "js_byte_view_proto": ("ByteView.prototype", "ByteView", CORE, "ByteView"),
    "js_byte_span_source_proto": (
        "ByteSpanSource.prototype", "ByteSpanSource", CORE, "ByteSpanSource"
    ),
    "js_bitmap_span_source_proto": (
        "BitmapSpanSource.prototype", "BitmapSpanSource", "bitmap", "BitmapSpanSource"
    ),
    "js_display_command_buffer_proto": (
        "DisplayCommandBuffer.prototype", "DisplayCommandBuffer", "bitmap", "DisplayCommandBuffer"
    ),
    "js_bitmap_proto": ("Bitmap.prototype", "Bitmap", "bitmap", "Bitmap"),
    "js_bitmap": ("bitmap", "BitmapModule", "bitmap", "bitmap"),
    "js_fs_volume_proto": ("FsVolume.prototype", "FsVolume", "fs", "FsVolume"),
    "js_framework": ("framework", "FrameworkModule", "fs", "framework"),
    "js_nvs": ("nvs", "NVSModule", "nvs", "nvs"),
    "js_gpio": ("gpio", "GpioModule", "gpio", "gpio"),
    "js_ledc": ("ledc", "LedcModule", "ledc", "ledc"),
    "js_adc": ("adc", "AdcModule", "adc", "adc"),
    "js_dac": ("dac", "DacModule", "dac", "dac"),
    "js_sys_time": ("sys.time", "SysTimeModule", CORE, "sys.time"),
    "js_sys": ("sys", "SysModule", CORE, "sys"),
    "js_runtime_logs": (
        "runtimeLogs", "RuntimeLogsModule", "runtimeLogs", "runtimeLogs"
    ),
    "js_i2c_bus_proto": ("I2CBus.prototype", "I2CBus", "i2c", "I2CBus"),
    "js_i2c_device_proto": (
        "I2CDevice.prototype", "I2CDevice", "i2c", "I2CDevice"
    ),
    "js_i2c": ("i2c", "I2CModule", "i2c", "i2c"),
    "js_spi_bus_proto": ("SPIBus.prototype", "SPIBus", "spi", "SPIBus"),
    "js_spi_device_proto": (
        "SPIDevice.prototype", "SPIDevice", "spi", "SPIDevice"
    ),
    "js_spi": ("spi", "SPIModule", "spi", "spi"),
    "js_uart_port_proto": ("UARTPort.prototype", "UARTPort", "uart", "UARTPort"),
    "js_uart": ("uart", "UARTModule", "uart", "uart"),
    "js_rmt_symbol_buffer_proto": (
        "RMTSymbolBuffer.prototype", "RMTSymbolBuffer", "rmt", "RMTSymbolBuffer"
    ),
    "js_rmt_channel_proto": (
        "RMTChannel.prototype", "RMTChannel", "rmt", "RMTChannel"
    ),
    "js_rmt": ("rmt", "RMTModule", "rmt", "rmt"),
    "js_i2s_channel_proto": (
        "I2SChannel.prototype", "I2SChannel", "i2s", "I2SChannel"
    ),
    "js_i2s": ("i2s", "I2SModule", "i2s", "i2s"),
    "js_camera_proto": ("Camera.prototype", "Camera", "camera", "Camera"),
    "js_camera_frame_proto": (
        "CameraFrame.prototype", "CameraFrame", "camera", "CameraFrame"
    ),
    "js_camera_module": ("camera", "CameraModule", "camera", "camera"),
    "js_usb_serial": (
        "usbSerial", "USBSerialModule", "usbSerial", "usbSerial"
    ),
    "js_usb_serial_handle_proto": (
        "USBSerialHandle.prototype", "USBSerialTextHandle", "usbSerial",
        "usbSerial"
    ),
    "js_rpc_codec_proto": (
        "RPCCodec.prototype", "RPCCodec", "rpc", "RPCCodec"
    ),
    "js_rpc_decoder_proto": (
        "RPCDecoder.prototype", "RPCDecoder", "rpc", "RPCDecoder"
    ),
    "js_rpc": ("rpc", "RPCModule", "rpc", "rpc"),
    "js_tcp_socket_proto": (
        "TCPSocket.prototype", "TCPSocket", "socket", "TCPSocket"
    ),
    "js_tcp_listener_proto": (
        "TCPListener.prototype", "TCPListener", "socket", "TCPListener"
    ),
    "js_udp_socket_proto": (
        "UDPSocket.prototype", "UDPSocket", "socket", "UDPSocket"
    ),
    "js_socket": ("socket", "SocketModule", "socket", "socket"),
    "js_websocket_client": (
        "websocketClient", "WebSocketClientModule", "websocket", "websocketClient"
    ),
    "js_websocket_handle_proto": (
        "WebSocketClientHandle.prototype", "WebSocketClientHandle", "websocket",
        "websocketClient"
    ),
    "js_wifi": ("wifi", "WiFiModule", "wifi", "wifi"),
    "js_wifi_csi_session_proto": (
        "WiFiCsiSession.prototype", "WiFiCsiSession", "wifiCsi",
        "WiFiCsiSession"
    ),
    "js_wifi_csi_frame_proto": (
        "WiFiCsiFrame.prototype", "WiFiCsiFrame", "wifiCsi", "WiFiCsiFrame"
    ),
    "js_wifi_csi_batch_proto": (
        "WiFiCsiBatch.prototype", "WiFiCsiBatch", "wifiCsi", "WiFiCsiBatch"
    ),
    "js_wifi_csi": ("wifiCsi", "WiFiCsiModule", "wifiCsi", "wifiCsi"),
    "js_espnow_session_proto": (
        "EspNowSession.prototype", "EspNowSession", "espNow", "EspNowSession"
    ),
    "js_espnow_peer_proto": (
        "EspNowPeer.prototype", "EspNowPeer", "espNow", "EspNowPeer"
    ),
    "js_espnow": ("espNow", "EspNowModule", "espNow", "espNow"),
    "js_ble_adapter_proto": (
        "BLEAdapter.prototype", "BLEAdapter", "ble", "BLEAdapter"
    ),
    "js_ble_scanner_proto": (
        "BLEScanner.prototype", "BLEScanner", "ble", "BLEScanner"
    ),
    "js_ble_advertiser_proto": (
        "BLEAdvertiser.prototype", "BLEAdvertiser", "ble", "BLEAdvertiser"
    ),
    "js_ble_connection_proto": (
        "BLEConnection.prototype", "BLEConnection", "ble", "BLEConnection"
    ),
    "js_ble_notification_proto": (
        "BLENotificationStream.prototype", "BLENotificationStream", "ble",
        "BLENotificationStream"
    ),
    "js_ble_gatt_server_proto": (
        "BLEGattServer.prototype", "BLEGattServer", "ble", "BLEGattServer"
    ),
    "js_ble_local_characteristic_proto": (
        "BLELocalCharacteristic.prototype", "BLELocalCharacteristic", "ble",
        "BLELocalCharacteristic"
    ),
    "js_ble": ("ble", "BLEModule", "ble", "ble"),
    "js_net": ("net", "NetModule", "net", "net"),
    "js_http": ("http", "HttpModule", "http", "http"),
    "js_http_server_proto": (
        "HttpServer.prototype", "HttpServer", "httpServer", "HttpServer"
    ),
    "js_global_object_extra": ("global", "global", CORE, "Global Helpers"),
}

# class name: (global name, TypeScript declaration, feature, docs owner token)
CLASSES = {
    "Headers": ("Headers", "Headers", CORE, "Headers"),
    "Request": ("Request", "Request", CORE, "Request"),
    "Response": ("Response", "Response", CORE, "Response"),
    "Future": ("Future", "Future", CORE, "Future"),
    "EventQueue": ("EventQueue", "EventQueue", CORE, "EventQueue"),
    "Stream": ("Stream", "Stream", CORE, "Stream"),
    "ByteView": ("_ByteView", "ByteView", CORE, "ByteView"),
    "ByteSpanSource": (
        "_ByteSpanSource", "ByteSpanSource", CORE, "ByteSpanSource"
    ),
    "BitmapSpanSource": (
        "_BitmapSpanSource", "BitmapSpanSource", "bitmap", "BitmapSpanSource"
    ),
    "DisplayFont": ("DisplayFont", "DisplayFont", "bitmap", "DisplayFont"),
    "DisplayCommandBuffer": (
        "DisplayCommandBuffer", "DisplayCommandBuffer", "bitmap", "DisplayCommandBuffer"
    ),
    "Bitmap": ("Bitmap", "Bitmap", "bitmap", "Bitmap"),
    "FsVolume": ("FsVolume", "FsVolume", "fs", "FsVolume"),
    "I2CBus": ("I2CBus", "I2CBus", "i2c", "I2CBus"),
    "I2CDevice": ("I2CDevice", "I2CDevice", "i2c", "I2CDevice"),
    "SPIBus": ("SPIBus", "SPIBus", "spi", "SPIBus"),
    "SPIDevice": ("SPIDevice", "SPIDevice", "spi", "SPIDevice"),
    "UARTPort": ("UARTPort", "UARTPort", "uart", "UARTPort"),
    "RMTSymbolBuffer": (
        "RMTSymbolBuffer", "RMTSymbolBuffer", "rmt", "RMTSymbolBuffer"
    ),
    "RMTChannel": ("RMTChannel", "RMTChannel", "rmt", "RMTChannel"),
    "I2SChannel": ("I2SChannel", "I2SChannel", "i2s", "I2SChannel"),
    "Camera": ("Camera", "Camera", "camera", "Camera"),
    "CameraFrame": ("CameraFrame", "CameraFrame", "camera", "CameraFrame"),
    "RPCCodec": ("RPCCodec", "RPCCodec", "rpc", "RPCCodec"),
    "RPCDecoder": ("RPCDecoder", "RPCDecoder", "rpc", "RPCDecoder"),
    "TCPSocket": ("TCPSocket", "TCPSocket", "socket", "TCPSocket"),
    "TCPListener": ("TCPListener", "TCPListener", "socket", "TCPListener"),
    "UDPSocket": ("UDPSocket", "UDPSocket", "socket", "UDPSocket"),
    "HttpServer": ("HttpServer", "HttpServer", "httpServer", "HttpServer"),
    "EspNowSession": (
        "EspNowSession", "EspNowSession", "espNow", "EspNowSession"
    ),
    "EspNowPeer": ("EspNowPeer", "EspNowPeer", "espNow", "EspNowPeer"),
    "WiFiCsiSession": (
        "WiFiCsiSession", "WiFiCsiSession", "wifiCsi", "WiFiCsiSession"
    ),
    "WiFiCsiFrame": (
        "WiFiCsiFrame", "WiFiCsiFrame", "wifiCsi", "WiFiCsiFrame"
    ),
    "WiFiCsiBatch": (
        "WiFiCsiBatch", "WiFiCsiBatch", "wifiCsi", "WiFiCsiBatch"
    ),
    "BLEAdapter": ("BLEAdapter", "BLEAdapter", "ble", "BLEAdapter"),
    "BLEScanner": ("BLEScanner", "BLEScanner", "ble", "BLEScanner"),
    "BLEAdvertiser": (
        "BLEAdvertiser", "BLEAdvertiser", "ble", "BLEAdvertiser"
    ),
    "BLEConnection": (
        "BLEConnection", "BLEConnection", "ble", "BLEConnection"
    ),
    "USBSerialHandle": (
        "USBSerialHandle", "USBSerialTextHandle", "usbSerial", "usbSerial"
    ),
    "WebSocketClientHandle": (
        "WebSocketClientHandle", "WebSocketClientHandle", "websocket",
        "websocketClient"
    ),
    "BLENotificationStream": (
        "BLENotificationStream", "BLENotificationStream", "ble",
        "BLENotificationStream"
    ),
    "BLEGattServer": (
        "BLEGattServer", "BLEGattServer", "ble", "BLEGattServer"
    ),
    "BLELocalCharacteristic": (
        "BLELocalCharacteristic", "BLELocalCharacteristic", "ble",
        "BLELocalCharacteristic"
    ),
}

METHOD_FEATURES = {
    ("sys.time", "sync"): "net",
    ("http", "server"): "httpServer",
    ("global", "fetch"): "http",
}

INTERNAL_METHODS = {("sys", "_deferIdle")}

BASE_GLOBALS = (
    ("print", 1, "js_print"),
    ("gc", 0, "js_gc"),
    ("load", 1, "js_load"),
    ("setTimeout", 2, "js_setTimeout"),
    ("clearTimeout", 1, "js_clearTimeout"),
)

FUTURE_CORE_SURFACES = {"Future", "Future.prototype"}

FUTURE_REGISTRATIONS: dict[tuple[str, str], tuple[str, str]] = {}


def register_future(
    surface: str, methods: tuple[str, ...], source: str, function: str
) -> None:
    for method in methods:
        FUTURE_REGISTRATIONS[(surface, method)] = (source, function)


register_future(
    "EventQueue.prototype", ("receive",),
    "components/esp32_mquickjs/src/core/esp32_mquickjs_event_queue.c",
    "esp32_mquickjs_init_event_queue_runtime",
)
register_future(
    "Stream.prototype", ("read", "write", "flush", "close", "seek"),
    "components/esp32_mquickjs/src/core/esp32_mquickjs_stream.c",
    "esp32_mquickjs_init_stream_runtime",
)
register_future(
    "FsVolume.prototype",
    ("open", "list", "stat", "exists", "readText", "writeText", "appendText", "remove", "rename", "mkdir"),
    "components/esp32_mquickjs/src/modules/fs/esp32_mquickjs_fs.c",
    "esp32_mquickjs_init_fs_runtime",
)
register_future(
    "nvs", ("getString", "setString", "erase", "clear"),
    "components/esp32_mquickjs/src/modules/nvs/esp32_mquickjs_nvs.c",
    "nvs_register_future_drivers",
)
register_future(
    "sys.time", ("sync",),
    "components/esp32_mquickjs/src/modules/time/esp32_mquickjs_time.c",
    "esp32_mquickjs_init_time_runtime",
)
register_future(
    "I2CBus.prototype", ("scan",),
    "components/esp32_mquickjs/src/modules/i2c/esp32_mquickjs_i2c.c",
    "i2c_register_future_drivers",
)
register_future(
    "I2CDevice.prototype", ("write", "writeSegments", "writeBatch", "read", "writeRead"),
    "components/esp32_mquickjs/src/modules/i2c/esp32_mquickjs_i2c.c",
    "i2c_register_future_drivers",
)
register_future(
    "SPIDevice.prototype", ("transfer", "write", "writeChunks", "writeSource", "read"),
    "components/esp32_mquickjs/src/modules/spi/esp32_mquickjs_spi.c",
    "spi_register_future_drivers",
)
register_future(
    "UARTPort.prototype", ("write", "writeChunks", "writeSource", "read", "flush"),
    "components/esp32_mquickjs/src/modules/uart/esp32_mquickjs_uart.c",
    "uart_register_future_drivers",
)
register_future(
    "RMTChannel.prototype", ("transmit", "receive"),
    "components/esp32_mquickjs/src/modules/rmt/esp32_mquickjs_rmt.c",
    "rmt_register_future_drivers",
)
register_future(
    "I2SChannel.prototype", ("read", "write"),
    "components/esp32_mquickjs/src/modules/i2s/esp32_mquickjs_i2s.c",
    "i2s_register_future_driver",
)
register_future(
    "Camera.prototype", ("capture", "close"),
    "components/esp32_mquickjs/src/modules/camera/esp32_mquickjs_camera.c",
    "camera_register_future_driver",
)
register_future(
    "bitmap", ("convert",),
    "components/esp32_mquickjs/src/modules/bitmap/esp32_mquickjs_bitmap_image.c",
    "esp32_mquickjs_init_bitmap_runtime",
)
register_future(
    "Bitmap.prototype", ("blit",),
    "components/esp32_mquickjs/src/modules/bitmap/esp32_mquickjs_bitmap_image.c",
    "esp32_mquickjs_init_bitmap_runtime",
)
register_future(
    "usbSerial", ("send",),
    "components/esp32_mquickjs/src/modules/usb_serial/esp32_mquickjs_usb_serial.c",
    "usb_serial_register_future_driver",
)
register_future(
    "TCPSocket.prototype", ("connect", "send", "recv"),
    "components/esp32_mquickjs/src/modules/socket/esp32_mquickjs_socket.c",
    "socket_register_future_drivers",
)
register_future(
    "TCPListener.prototype", ("accept",),
    "components/esp32_mquickjs/src/modules/socket/esp32_mquickjs_socket.c",
    "socket_register_future_drivers",
)
register_future(
    "UDPSocket.prototype", ("sendTo", "receiveFrom"),
    "components/esp32_mquickjs/src/modules/socket/esp32_mquickjs_socket.c",
    "socket_register_future_drivers",
)
register_future(
    "websocketClient", ("send",),
    "components/esp32_mquickjs/src/modules/websocket/esp32_mquickjs_websocket.c",
    "websocket_register_future_driver",
)
register_future(
    "wifi", ("connect", "disconnect", "scan"),
    "components/esp32_mquickjs/src/modules/wifi/esp32_mquickjs_wifi_future.c",
    "esp32_mquickjs_init_wifi_future_runtime",
)
register_future(
    "espNow", ("open",),
    "components/esp32_mquickjs/src/modules/espnow/esp32_mquickjs_espnow.c",
    "espnow_register_future_drivers",
)
register_future(
    "EspNowSession.prototype",
    ("receive", "addPeer", "broadcast", "setPowerSave", "close"),
    "components/esp32_mquickjs/src/modules/espnow/esp32_mquickjs_espnow.c",
    "espnow_register_future_drivers",
)
register_future(
    "EspNowPeer.prototype", ("send", "update", "close"),
    "components/esp32_mquickjs/src/modules/espnow/esp32_mquickjs_espnow.c",
    "espnow_register_future_drivers",
)
register_future(
    "ble", ("open",),
    "components/esp32_mquickjs/src/modules/ble/esp32_mquickjs_ble.c",
    "ble_register_future_drivers",
)
register_future(
    "BLEAdapter.prototype",
    ("scan", "connect", "advertise", "removeBond", "clearBonds", "close"),
    "components/esp32_mquickjs/src/modules/ble/esp32_mquickjs_ble.c",
    "ble_register_future_drivers",
)
register_future(
    "BLEScanner.prototype", ("receive", "close"),
    "components/esp32_mquickjs/src/modules/ble/esp32_mquickjs_ble.c",
    "ble_register_future_drivers",
)
register_future(
    "BLEAdvertiser.prototype", ("receive", "close"),
    "components/esp32_mquickjs/src/modules/ble/esp32_mquickjs_ble.c",
    "ble_register_future_drivers",
)
register_future(
    "BLEConnection.prototype",
    ("receive", "pair", "exchangeMtu", "readRssi", "discover", "close"),
    "components/esp32_mquickjs/src/modules/ble/esp32_mquickjs_ble.c",
    "ble_register_future_drivers",
)
register_future(
    "BLECharacteristic.prototype", ("read", "write", "subscribe"),
    "components/esp32_mquickjs/src/modules/ble/esp32_mquickjs_ble.c",
    "ble_register_future_drivers",
)
register_future(
    "BLEDescriptor.prototype", ("read", "write"),
    "components/esp32_mquickjs/src/modules/ble/esp32_mquickjs_ble.c",
    "ble_register_future_drivers",
)
register_future(
    "BLENotificationStream.prototype", ("receive", "close"),
    "components/esp32_mquickjs/src/modules/ble/esp32_mquickjs_ble.c",
    "ble_register_future_drivers",
)
register_future(
    "BLELocalCharacteristic.prototype", ("notify",),
    "components/esp32_mquickjs/src/modules/ble/esp32_mquickjs_ble.c",
    "ble_register_future_drivers",
)
register_future(
    "http", ("fetch",),
    "components/esp32_mquickjs/src/modules/http/esp32_mquickjs_http_future.c",
    "esp32_mquickjs_init_http_future_runtime",
)
register_future(
    "global", ("fetch",),
    "components/esp32_mquickjs/src/modules/http/esp32_mquickjs_http_future.c",
    "esp32_mquickjs_init_http_future_runtime",
)
register_future(
    "HttpServer.prototype", ("receive", "respond"),
    "components/esp32_mquickjs/src/modules/http/esp32_mquickjs_http_server.c",
    "esp32_mquickjs_init_http_server_runtime",
)


ARRAY_RE = re.compile(
    r"static const JSPropDef\s+(\w+)\[\]\s*=\s*\{(.*?)\n\};", re.DOTALL
)
FUNCTION_RE = re.compile(
    r'JS_CFUNC(?:_MAGIC)?_DEF\("([^"]+)",\s*(\d+),\s*(\w+)'
)
CLASS_RE = re.compile(
    r"static const JSClassDef\s+(\w+)\s*=\s*"
    r"JS_CLASS(?:_TRACE)?_DEF\(\"([^\"]+)\",\s*(\d+),\s*(\w+),\s*(JS_CLASS_\w+)",
    re.DOTALL,
)


def parse_stdlib_functions() -> dict[str, list[tuple[str, int, str]]]:
    source = STDLIB_PATH.read_text(encoding="utf-8")
    result: dict[str, list[tuple[str, int, str]]] = {}
    for match in ARRAY_RE.finditer(source):
        methods = [
            (name, int(arity), implementation)
            for name, arity, implementation in FUNCTION_RE.findall(match.group(2))
        ]
        if methods:
            result[match.group(1)] = methods
    return result


def feature_name(surface: str, method: str, default: str) -> str:
    feature = METHOD_FEATURES.get((surface, method), default)
    return CORE if feature == CORE else FEATURES[feature]


def function_entry(
    *,
    table: str,
    surface: str,
    declaration: str,
    feature: str,
    docs_owner: str,
    name: str,
    arity: int,
    implementation: str,
    source_file: str = "components/esp32_mquickjs/src/core/mqjs_stdlib_esp32.c",
) -> dict[str, object]:
    qualified = name if surface == "global" else f"{surface}.{name}"
    key = (surface, name)
    visibility = "internal" if key in INTERNAL_METHODS else "public"
    registration = FUTURE_REGISTRATIONS.get(key)
    if registration is not None:
        execution = "nativeFuture"
    elif surface in FUTURE_CORE_SURFACES:
        execution = "futureCore"
    elif visibility == "internal":
        execution = "internal"
    else:
        execution = "immediate"
    entry: dict[str, object] = {
        "qualifiedName": qualified,
        "surface": surface,
        "name": name,
        "arity": arity,
        "implementation": implementation,
        "visibility": visibility,
        "feature": feature_name(surface, name, feature),
        "execution": execution,
        "stdlib": {"file": source_file, "table": table},
        "typescript": {
            "file": "types/esp32qjs-c-api.d.ts",
            "declaration": declaration,
        },
        "documentation": {
            "file": "docs/c-api.md",
            "ownerToken": docs_owner,
        },
    }
    if registration is not None:
        entry["futureRegistration"] = {
            "file": registration[0],
            "function": registration[1],
        }
    return entry


def build_manifest() -> dict[str, object]:
    parsed = parse_stdlib_functions()
    missing_metadata = sorted(set(parsed) - set(SURFACES))
    stale_metadata = sorted(set(SURFACES) - set(parsed))
    if missing_metadata or stale_metadata:
        raise ValueError(
            "stdlib surface metadata mismatch: "
            f"missing={missing_metadata} stale={stale_metadata}"
        )
    functions: list[dict[str, object]] = []
    for table, (surface, declaration, feature, docs_owner) in SURFACES.items():
        for name, arity, implementation in parsed[table]:
            functions.append(
                function_entry(
                    table=table,
                    surface=surface,
                    declaration=declaration,
                    feature=feature,
                    docs_owner=docs_owner,
                    name=name,
                    arity=arity,
                    implementation=implementation,
                )
            )
    for name, arity, implementation in BASE_GLOBALS:
        functions.append(
            function_entry(
                table="js_global_object_base",
                surface="global",
                declaration="global",
                feature=CORE,
                docs_owner="Global Helpers",
                name=name,
                arity=arity,
                implementation=implementation,
                source_file="components/esp32_mquickjs/vendor/mquickjs/mqjs_stdlib.c",
            )
        )
    functions.sort(key=lambda entry: str(entry["qualifiedName"]))
    stdlib_source = STDLIB_PATH.read_text(encoding="utf-8")
    parsed_classes = {
        class_name: {
            "definition": definition,
            "arity": int(arity),
            "implementation": implementation,
            "classId": class_id,
        }
        for definition, class_name, arity, implementation, class_id
        in CLASS_RE.findall(stdlib_source)
    }
    missing_classes = sorted(set(parsed_classes) - set(CLASSES))
    stale_classes = sorted(set(CLASSES) - set(parsed_classes))
    if missing_classes or stale_classes:
        raise ValueError(
            "stdlib class metadata mismatch: "
            f"missing={missing_classes} stale={stale_classes}"
        )
    classes: list[dict[str, object]] = []
    for class_name, (global_name, declaration, feature, docs_owner) in CLASSES.items():
        parsed_class = parsed_classes[class_name]
        classes.append(
            {
                "name": class_name,
                "globalName": global_name,
                "arity": parsed_class["arity"],
                "implementation": parsed_class["implementation"],
                "classId": parsed_class["classId"],
                "feature": CORE if feature == CORE else FEATURES[feature],
                "stdlib": {
                    "file": "components/esp32_mquickjs/src/core/mqjs_stdlib_esp32.c",
                    "definition": parsed_class["definition"],
                },
                "typescript": {
                    "file": "types/esp32qjs-c-api.d.ts",
                    "declaration": declaration,
                },
                "documentation": {
                    "file": "docs/c-api.md",
                    "ownerToken": docs_owner,
                },
            }
        )
    return {
        "version": 1,
        "scope": "ESP32QJS first-party callable native API",
        "classes": classes,
        "functions": functions,
    }


def extract_braced_declaration(source: str, declaration: str) -> str:
    match = re.search(rf"\b(?:interface|class)\s+{re.escape(declaration)}\b[^{{]*\{{", source)
    if match is None:
        raise ValueError(f"missing TypeScript declaration {declaration}")
    start = source.index("{", match.start())
    depth = 0
    for index in range(start, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start : index + 1]
    raise ValueError(f"unterminated TypeScript declaration {declaration}")


def extract_c_function(source: str, function: str) -> str:
    offset = 0
    needle = f"{function}("
    while True:
        start = source.find(needle, offset)
        if start < 0:
            raise ValueError(f"missing C function {function}")
        paren = source.find("(", start)
        depth = 0
        close = -1
        for index in range(paren, len(source)):
            if source[index] == "(":
                depth += 1
            elif source[index] == ")":
                depth -= 1
                if depth == 0:
                    close = index
                    break
        cursor = close + 1
        while cursor < len(source) and source[cursor].isspace():
            cursor += 1
        if cursor < len(source) and source[cursor] == "{":
            depth = 0
            for index in range(cursor, len(source)):
                if source[index] == "{":
                    depth += 1
                elif source[index] == "}":
                    depth -= 1
                    if depth == 0:
                        return source[cursor : index + 1]
            raise ValueError(f"unterminated C function {function}")
        offset = start + len(needle)


def validate_manifest(manifest: dict[str, object]) -> list[str]:
    errors: list[str] = []
    types = TYPES_PATH.read_text(encoding="utf-8")
    docs = DOCS_PATH.read_text(encoding="utf-8")
    kconfig = KCONFIG_PATH.read_text(encoding="utf-8")
    declaration_cache: dict[str, str] = {}
    function_cache: dict[tuple[str, str], str] = {}
    stdlib = STDLIB_PATH.read_text(encoding="utf-8")

    for raw_class in manifest["classes"]:  # type: ignore[index]
        class_entry = raw_class  # type: ignore[assignment]
        name = str(class_entry["name"])
        global_name = str(class_entry["globalName"])
        feature = str(class_entry["feature"])
        declaration = str(class_entry["typescript"]["declaration"])  # type: ignore[index]
        try:
            extract_braced_declaration(types, declaration)
        except ValueError as error:
            errors.append(f"{name}: {error}")
        if re.search(rf"\bconst\s+{re.escape(global_name)}\s*:", types) is None:
            errors.append(f"{name}: missing global TypeScript constructor {global_name}")
        definition = str(class_entry["stdlib"]["definition"])  # type: ignore[index]
        if f'JS_PROP_CLASS_DEF("{global_name}", &{definition})' not in stdlib:
            errors.append(f"{name}: class is not registered as global {global_name}")
        owner_token = str(class_entry["documentation"]["ownerToken"])  # type: ignore[index]
        if owner_token not in docs:
            errors.append(f"{name}: missing C API documentation owner {owner_token}")
        if feature != CORE:
            symbol = feature.removeprefix("CONFIG_")
            if f"config {symbol}" not in kconfig:
                errors.append(f"{name}: missing Kconfig symbol {feature}")

    for raw_entry in manifest["functions"]:  # type: ignore[index]
        entry = raw_entry  # type: ignore[assignment]
        qualified = str(entry["qualifiedName"])
        name = str(entry["name"])
        feature = str(entry["feature"])
        if feature != CORE:
            symbol = feature.removeprefix("CONFIG_")
            if f"config {symbol}" not in kconfig:
                errors.append(f"{qualified}: missing Kconfig symbol {feature}")
            if feature not in stdlib:
                errors.append(f"{qualified}: feature {feature} does not gate stdlib")

        if entry["visibility"] == "public":
            declaration = str(entry["typescript"]["declaration"])  # type: ignore[index]
            if declaration == "global":
                type_source = types
                pattern = rf"\bfunction\s+{re.escape(name)}\s*(?:<[^>]*>)?\s*\("
            else:
                try:
                    type_source = declaration_cache.setdefault(
                        declaration, extract_braced_declaration(types, declaration)
                    )
                except ValueError as error:
                    errors.append(f"{qualified}: {error}")
                    type_source = ""
                pattern = rf"\b{re.escape(name)}\s*\??\s*(?:<[^>]*>)?\s*\("
            if not re.search(pattern, type_source):
                errors.append(
                    f"{qualified}: missing method in TypeScript {declaration}"
                )
            owner_token = str(entry["documentation"]["ownerToken"])  # type: ignore[index]
            if owner_token not in docs:
                errors.append(f"{qualified}: missing documentation owner {owner_token}")
            if re.search(rf"\b{re.escape(name)}\s*\(", docs) is None:
                errors.append(f"{qualified}: missing callable in C API docs")

        if entry["execution"] == "nativeFuture":
            registration = entry["futureRegistration"]  # type: ignore[index]
            source_path = ROOT / str(registration["file"])
            function = str(registration["function"])
            cache_key = (str(source_path), function)
            try:
                body = function_cache.setdefault(
                    cache_key,
                    extract_c_function(
                        source_path.read_text(encoding="utf-8"), function
                    ),
                )
            except (OSError, ValueError) as error:
                errors.append(f"{qualified}: {error}")
                continue
            if f'"{name}"' not in body:
                errors.append(
                    f"{qualified}: Future registration {function} does not select the method"
                )
            if (
                "esp32_mquickjs_future_register_driver" not in body
                and "esp32_mquickjs_event_queue_register_receive_alias" not in body
            ):
                errors.append(
                    f"{qualified}: Future registration {function} installs no driver"
                )
    return errors


def render_manifest(manifest: dict[str, object]) -> str:
    return json.dumps(manifest, indent=2, ensure_ascii=False) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--write", action="store_true", help="rewrite api-manifest.json")
    parser.add_argument("--check", action="store_true", help="verify the checked-in manifest")
    args = parser.parse_args()
    if not args.write and not args.check:
        args.check = True

    try:
        manifest = build_manifest()
    except ValueError as error:
        print(f"API manifest generation failed: {error}", file=sys.stderr)
        return 1
    rendered = render_manifest(manifest)
    if args.write:
        MANIFEST_PATH.write_text(rendered, encoding="utf-8")
    if args.check:
        if not MANIFEST_PATH.is_file() or MANIFEST_PATH.read_text(encoding="utf-8") != rendered:
            print(
                "api-manifest.json is stale; run scripts/generate_api_manifest.py --write",
                file=sys.stderr,
            )
            return 1
        errors = validate_manifest(manifest)
        if errors:
            print("API manifest validation failed:", file=sys.stderr)
            for error in errors:
                print(f"- {error}", file=sys.stderr)
            return 1
    print(
        "API manifest: passed "
        f"classes={len(manifest['classes'])} functions={len(manifest['functions'])}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
