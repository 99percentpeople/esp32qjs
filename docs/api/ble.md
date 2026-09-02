# `ble` Module

`ble` exposes the generic ESP-NimBLE central, peripheral, observer,
broadcaster, GATT, and security surface when `sys.info.features.ble` is
enabled. It does not parse advertising structures, define a product GATT
profile, or implement provisioning policy.

- `ble.capabilities()` reports v1/target/ESP-IDF identity, compiled roles,
  connection/MTU limits, bonding, privacy, and advertising capabilities.
- `ble.open(options?)` opens the runtime singleton. It strictly validates roles,
  device name, own-address policy, preferred MTU, connection limit, security,
  and an optional static native-cached GATT Server definition.
- `BLEAdapter.scan()` returns a `BLEScanner`. Reports carry raw legacy payloads
  in owned `ByteView` objects through a bounded DROP_NEWEST EventQueue.
- `BLEAdapter.advertise()` returns a `BLEAdvertiser`. It accepts only raw
  advertising and scan-response bytes; an incoming peripheral connection is
  returned as a generation-checked `BLEConnection`.
- `BLEAdapter.connect()` creates a central `BLEConnection`. `status()` reports
  link/security state and the per-connection GATT lane; `receive()` carries
  disconnect, MTU, security, and pairing-request control events.
- `BLEConnection.discover()` returns one flat value snapshot containing
  `services`, `characteristics`, and `descriptors` records plus a discovery
  generation. It does not allocate one native reference per attribute.
  `readHandle()`, `writeHandle()`, and `subscribeHandle()` validate handles
  against the current discovery. Attribute operations, MTU exchange, pairing,
  and close are native Future operations serialized per connection.
- `BLEConnection.pair()`, `respondPairing()`, `exchangeMtu()`, and `readRssi()`
  expose explicit security, MTU, and signal operations. A JavaScript Library
  may wrap the flat records into service/characteristic/descriptor façade
  objects; native firmware keeps only the connection and notification stream
  resource handles.
- `BLENotificationStream` owns a bounded notification/indication EventQueue.
  Each payload is an owned `ByteView`; close it after use. Closing the stream
  first disables its CCCD.
- `BLEGattServer` exposes the static definition captured by `ble.open()`.
  `watch()` returns bounded remote-write/subscription events;
  `characteristic(id)` returns a generation-checked local handle, while
  `BLELocalCharacteristic.value()`, `setValue()`, and `notify()` operate on the
  native cache without calling JavaScript from NimBLE callbacks.
- `BLEAdapter.bonds()`, `removeBond()`, and `clearBonds()` operate on the NimBLE
  store. Pairing requests must be answered before `expiresAtUs`.
- `BLEAdapter.server()` returns the configured `BLEGattServer` or `null`.
  `BLEScanner.stats()`, `BLEAdvertiser.stats()`, `BLEConnection.stats()`, and
  `BLENotificationStream.stats()` return their bounded EventQueue counters.
- `BLEAdapter.close()` stops GAP sources and connections, then stops and
  deinitializes NimBLE on a worker before releasing native pools. A host-stop
  failure retains the started host and initialized port; a later deinit failure
  retains the initialized port alone. The adapter enters a cleanup-only failed
  state and another `close()` can retry. Only full cleanup invalidates every BLE
  handle from that adapter generation.

All one-shot BLE operations support the usual cooperative direct-call form and
the explicit `Future.call(...)` form. GAP start/stop operations share one lane,
GATT procedures use one lane per connection, and server indications use a
confirmation lane. Timeouts do not silently reduce MTU, discard security, or
fall back to another address policy.

```js
var adapter = ble.open({ roles: ["central"], deviceName: "ESP32QJS" });
try {
  var scanner = adapter.scan({ active: true, durationMs: 5000, capacity: 16 });
  try {
    var report = scanner.receive(1000);
    if (report !== null) {
      try {
        print(report.peer.address, report.rssi, report.data.length);
      } finally {
        report.data.close();
      }
    }
  } finally {
    scanner.close();
  }
} finally {
  adapter.close();
}
```
