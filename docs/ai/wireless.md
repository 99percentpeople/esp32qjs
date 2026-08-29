# Local Wireless API

## ESP-NOW

`espNow` is a generic station-interface ESP-NOW transport. It does not provide
application acknowledgements, retries, deduplication, fragmentation, routing,
Mesh behavior, provisioning, or a product message schema.

- `espNow.capabilities()` reports compile-time peer and payload limits.
  `peerRateConfig` is currently `false`; no native peer-rate control is exposed
  until automatic-rate restoration can survive peer update and timeout rebuild.
- `espNow.open(options?)` creates the only session in the current runtime.
- `session.receive(timeoutMs?)` consumes a bounded receive queue; each event
  includes source/destination addresses, channel, RSSI, timestamp, sequence,
  broadcast state, and an owned `ByteView`.
- `session.stats()` reports the bounded receive EventQueue counters.
- `session.addPeer(options)` creates an application peer; `peer.update()` and
  `peer.close()` modify or remove it.
- `session.peer(address)` returns one generation-checked handle or `null`, and
  `session.peers()` returns key-free peer status snapshots.
- `peer.send(data, options?)` and `session.broadcast(data, options?)` share one
  FIFO transmit lane. `macDelivered` is only the ESP-NOW MAC result, not an
  application acknowledgement.
- `session.status()` exposes channel synchronization, bounded counters, peer
  counts, pending sends, timeout recovery, and power-save configuration.
- `session.setPowerSave(options)` updates the wake window and interval. Pass
  `{ enabled: false }` to restore the ESP-IDF always-awake/default interval
  settings without closing the session.

When `powerSave` is omitted, opening ESP-NOW leaves the ESP-IDF connectionless
power-save defaults untouched. Native wake-window and interval values are
written only for an explicit power-save configuration or when restoring a
session that previously changed them.

Use `channel: "current"` when sharing the station radio with Wi-Fi. A fixed
channel is rejected when it conflicts with the current station channel; the
framework never disconnects Wi-Fi or performs hidden off-channel sends.

Encrypted peers require an explicit 16-byte PMK at `open()` and a distinct
16-byte LMK at `addPeer()`. Keys are copied into native state, cleared from
temporary buffers, and never returned by status, peer snapshots, or errors.

```js
var session = espNow.open({
  channel: "current",
  receiveCapacity: 8,
  sendTimeoutMs: 1000
});

try {
  var peer = session.addPeer({
    address: "02:00:00:00:00:01",
    channel: "current"
  });
  var result = peer.send([1, 2, 3]);
  var incoming = session.receive(1000);
  if (incoming) {
    try {
      print(incoming.sourceAddress, incoming.data.length);
    } finally {
      incoming.data.close();
    }
  }
  print(result.macDelivered);
  peer.close();
} finally {
  session.close();
}
```

Closing a session invalidates every peer handle. A transmit callback timeout
reports `ESPNOW_RECOVERY_PENDING` while the native Future state and FIFO lane
remain retained. A worker waits for callbacks to become quiescent and rebuilds
the native ESP-NOW session before releasing either; recovery failure leaves the
session unavailable until it is closed and reopened.
Closing an enabled power-save session restores the native defaults before
ESP-NOW is deinitialized, so a later session never inherits hidden radio state.

## Bluetooth LE

`ble` is the generic ESP-NimBLE v1 API. Firmware exposes raw legacy advertising,
central/peripheral connections, bounded GATT client operations, independent
notification streams, a static native-cached GATT Server, pairing, and bonds.
Advertising-data parsing, product profiles, and provisioning flows belong in
Project or Board JavaScript Libraries.

- Open at most one adapter with `ble.open(options?)`; every returned handle is
  tied to that adapter generation.
- `adapter.scan()` and `adapter.advertise()` return EventQueue-backed sources.
  Close every report or notification `ByteView` after use.
- Use `adapter.connect(peer, options?)` for outbound connections and
  `advertiser.receive()` for incoming peripheral connections.
- Run discovery before using `BLEService`, `BLECharacteristic`, or
  `BLEDescriptor` handles. A later discovery or connection close invalidates
  the previous attribute handles.
- `characteristic.subscribe()` returns a separate bounded
  `BLENotificationStream`; notifications never consume the connection control
  queue.
- Declare a GATT Server only in `ble.open({ server: ... })`. Remote callbacks
  read/write native cached values and publish events without invoking JS.
- Pairing interaction arrives through `connection.receive()` and must be
  answered with `respondPairing()` before its deadline. Bond keys remain in the
  NimBLE store and never appear in status or errors.

```js
var adapter = ble.open({ roles: ["central"], deviceName: "ESP32QJS" });
try {
  var scanner = adapter.scan({ active: true, durationMs: 3000 });
  try {
    var event = scanner.receive(1000);
    if (event) {
      try {
        print(event.peer.address, event.rssi);
      } finally {
        event.data.close();
      }
    }
  } finally {
    scanner.close();
  }
} finally {
  adapter.close();
}
```

Direct calls preserve the cooperative synchronous appearance. Use
`Future.call(method, receiver, args)` when multiple independent operations must
be in flight. GAP procedures share one lane, while GATT procedures are
serialized per connection.

Native callback ownership, synchronization, and quiescent teardown rules are
recorded in the framework repository's `docs/wireless-concurrency.md`.
