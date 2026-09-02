# ESP32QJS API reference

These module documents are the sole user-facing and AI-facing JavaScript API
reference. The host snapshots the subset selected by the immutable Build
Context and exposes it as `doc://framework/<id>` resources. Humans read the
same Markdown files directly; there is no separate compact AI copy.

Always inspect `sys.info.features` and the current Artifact documentation index
instead of inferring compiled modules from an MCU or Board name. Exact static
shapes are also checked against
[`types/esp32qjs-c-api.d.ts`](../../types/esp32qjs-c-api.d.ts), while
[`api-manifest.json`](../../api-manifest.json) verifies that every registered
native callable points to one of these documents.

## Core runtime

- [Runtime and execution rules](runtime.md)
- [Global helpers](global-helpers.md)
- [Native operational errors](native-errors.md)
- [Futures](futures.md)
- [Event queues](event-queues.md)
- [Timers](timers.md)
- [Stream and native byte sources](stream.md)
- [Headers, Request, and Response](http-types.md)
- [System information, status, time, and control](sys.md)

## Storage and hardware

- [Filesystem](fs.md) and [secondary LittleFS](secondary-littlefs.md)
- [NVS](nvs.md)
- [Pin and resource rules](hardware-rules.md)
- [GPIO](gpio.md), [LEDC PWM](ledc.md), [ADC](adc.md), and [DAC](dac.md)
- [I2C](i2c.md), [SPI](spi.md), [UART](uart.md), and [RMT](rmt.md)

## Media and graphics

- [I2S](i2s.md)
- [Camera](camera.md)
- [Bitmap](bitmap.md) and the optional [JPEG decoder](bitmap-jpeg.md)

## Network and local wireless

- [Network interfaces](net.md), [Wi-Fi](wifi.md), and [TLS](tls.md)
- [HTTP](http.md), [raw sockets](socket.md), and
  [WebSocket client](websocket-client.md)
- [Binary RPC codec](rpc.md) and [USB Serial](usb-serial.md)
- [Wi-Fi CSI](wifi-csi.md) and its
  [wire protocol](wifi-csi-protocol.md)
- [ESP-NOW](esp-now.md) and [Bluetooth LE](ble.md)
- [Runtime logs](runtime-logs.md)

`docs.json` is the sole v1 selection manifest. `requiresAnyFeatures` names
native Build Context feature IDs; an empty list makes a document core, while a
non-empty list includes it when at least one named feature is selected.

Framework JavaScript libraries and Board wrappers remain outside this index.
Their owning repositories/manifests provide separate Artifact-bound
`doc://library/...` and `doc://board/...` resources.
