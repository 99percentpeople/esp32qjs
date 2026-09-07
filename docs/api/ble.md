# `ble` Module

`ble` exposes ESP-NimBLE central, peripheral, observer, broadcaster, GATT, and
security capabilities when `sys.info.features.ble` is enabled. Applications
use raw advertising bytes and flat GATT records, then implement their profile
policy in JavaScript.

## Capabilities and adapter

```text
ble.capabilities() -> {
  apiVersion: "v1",
  target: string,
  idfVersion: string,
  supported: true,
  classic: false,
  central: boolean,
  peripheral: boolean,
  observer: boolean,
  broadcaster: boolean,
  legacyAdvertising: true,
  extendedAdvertising: false,
  maxConnections: number,
  maxMtu: number,
  bonding: boolean,
  privacy: boolean,
  concurrentScanAdvertising: boolean
}
```

Open the runtime singleton with:

```text
ble.open(options?: {
  roles?: ("central" | "peripheral")[],
  deviceName?: string,
  ownAddressType?: "public" | "random-static" | "rpa",
  preferredMtu?: number,
  maxConnections?: number,
  security?: {
    bonding?: boolean,
    secureConnections?: boolean,
    mitm?: boolean,
    ioCapability?: "none" | "display-only" | "keyboard-only" |
                   "display-keyboard" | "display-yes-no",
    pairingTimeoutMs?: number
  },
  server?: BLEGattServerDefinition
}) -> BLEAdapter
```

The default roles are central and peripheral, the default name is `ESP32QJS`,
the preferred MTU is 256, and `maxConnections` uses the compiled capability.
`preferredMtu` is from 23 through `ble.capabilities().maxMtu`;
`pairingTimeoutMs` is 1000 through 60000. Security defaults to Secure
Connections with the compiled bonding policy, `mitm:false`, and
`ioCapability:"none"`.

`adapter.status()` returns:

```text
{
  open, synchronized, address, addressType, deviceName, roles,
  connections, maxConnections, scanning, advertising, bondedDevices,
  resetCount, droppedScanReports, droppedConnectionEvents,
  droppedServerEvents
}
```

All booleans, counts, and strings in this status are snapshots. BLE addresses
use `{ address: "aa:bb:cc:dd:ee:ff", type }`, where `type` is `"public"`,
`"random-static"`, `"random-private-resolvable"`, or
`"random-private-nonresolvable"`.

## Scanning

```text
adapter.scan(options?: {
  active?: boolean,
  intervalMs?: number,
  windowMs?: number,
  durationMs?: number,
  filterDuplicates?: boolean,
  limited?: boolean,
  capacity?: number
}) -> BLEScanner
```

Scanning defaults to active scanning with duplicate filtering. `intervalMs`
and `windowMs` accept 2.5 through 10240 milliseconds, with the window at most
the interval. `durationMs` accepts 1 through 2147483647; omitting it keeps the
scanner active until `close()`. `capacity` selects the bounded DROP_NEWEST
report queue up to the compiled scan limit.

`scanner.receive(timeoutMs?)` returns `null` at timeout or:

```text
{
  type: "report",
  sequence: number,
  timestampUs: number,
  peer: BLEAddress,
  eventType: "advertisement" | "scan-response" | "directed-advertisement",
  rssi: number,
  connectable: boolean,
  scannable: boolean,
  directed: boolean,
  data: ByteView
}
```

The report owns `data`; close that `ByteView` after inspecting or retaining the
bytes. `scanner.stats()` returns the common `EventQueueStats` shape.
`scanner.status()` returns `{ open, active, startedAtUs, reports, dropped,
malformed, stopReason }`, with `stopReason` equal to `"running"`,
`"completed"`, `"closed"`, or `"error"`.

## Advertising and incoming connections

```text
adapter.advertise(options: {
  connectable?: boolean,
  scannable?: boolean,
  intervalMinMs?: number,
  intervalMaxMs?: number,
  durationMs?: number,
  data: ByteSource,
  scanResponse?: ByteSource,
  capacity?: number
}) -> BLEAdvertiser
```

`data` is the required raw legacy advertising payload and may contain up to 31
bytes. `scanResponse` also accepts up to 31 bytes and is paired with
`scannable:true`. Supplied advertising intervals are 20 through 10240
milliseconds and the minimum is at most the maximum. `durationMs` has the same
range and indefinite behavior as scanning. `capacity` bounds queued incoming
connections up to `maxConnections`.

`advertiser.receive(timeoutMs?)` returns `null` or:

```text
{
  type: "connection",
  sequence: number,
  timestampUs: number,
  connection: BLEConnection
}
```

`advertiser.status()` returns `{ open, active, connectable, scannable,
incomingConnections, dropped, stopReason }`; its stop reason is `"running"`,
`"connected"`, `"completed"`, `"closed"`, or `"error"`.

## Central connections and control events

```text
adapter.connect(peer: BLEAddress, options?: {
  timeoutMs?: number,
  preferredMtu?: number
}) -> BLEConnection
```

`timeoutMs` is 1 through 60000. `preferredMtu` is 23 through the compiled
maximum. `connection.status()` returns:

```text
{
  open: boolean,
  connectionId: number,
  peer: BLEAddress,
  role: "central" | "peripheral",
  mtu: number,
  rssi: number | null,
  security: {
    encrypted: boolean,
    authenticated: boolean,
    bonded: boolean,
    secureConnections: boolean
  },
  gattQueued: number,
  gattActive: boolean
}
```

`connection.receive(timeoutMs?)` carries these control events:

```text
{ type: "disconnected", sequence, timestampUs, reasonCode, reasonName }
{ type: "mtu", sequence, timestampUs, mtu }
{ type: "security", sequence, timestampUs, status: SecurityStatus }
{
  type: "pairing-request",
  sequence,
  timestampUs,
  requestId,
  action: "display-passkey" | "input-passkey" | "numeric-comparison",
  passkey?: number,
  expiresAtUs
}
```

Connection control methods are:

```text
connection.pair({ timeoutMs?: number }?) -> SecurityStatus
connection.respondPairing(requestId, response: boolean | number) -> boolean
connection.exchangeMtu(mtu?: number, timeoutMs?: number) -> number
connection.readRssi(timeoutMs?: number) -> number
connection.stats() -> EventQueueStats
connection.close() -> boolean
```

Use a numeric passkey for `"input-passkey"` and a boolean decision for
`"numeric-comparison"` or `"display-passkey"`. Respond before `expiresAtUs`.

## Remote GATT discovery and attributes

```text
connection.discover(options?: {
  includeDescriptors?: boolean,
  maxServices?: number,
  maxCharacteristics?: number,
  maxDescriptors?: number,
  timeoutMs?: number
}) -> {
  generation: number,
  services: {
    uuid, startHandle, endHandle, characteristicStart, characteristicCount
  }[],
  characteristics: {
    uuid, declarationHandle, valueHandle, properties,
    serviceIndex, descriptorStart, descriptorCount
  }[],
  descriptors: { uuid, handle, characteristicIndex }[]
}
```

The three `max*` fields select positive bounded capacities up to the compiled
limits. A new discovery replaces the connection's previous generation. Use
the returned indices to relate services, characteristics, and descriptors.

```text
connection.readHandle(handle, {
  timeoutMs?: number,
  maxBytes?: number
}?) -> ByteView

connection.writeHandle(handle, data: ByteSource, {
  response?: boolean,
  timeoutMs?: number
}?) -> number

connection.subscribeHandle(valueHandle, cccdHandle, {
  mode?: "notify" | "indicate" | "auto",
  capacity?: number,
  timeoutMs?: number
}?) -> BLENotificationStream
```

Attribute handles come from the current discovery. Reads default to the
compiled maximum attribute size and return an owned `ByteView`. Writes default
to `response:true` and return the submitted byte count. Subscription capacity
is 1 through 32; `"auto"` selects notification when available and indication
for an indication-only characteristic.

`stream.receive(timeoutMs?)` returns `null` or `{ type:"value", sequence,
timestampUs, indication, data }`. Close the owned `data` view after use.
`stream.status()` returns `{ open, mode, received, dropped }`, `stats()` returns
`EventQueueStats`, and `close()` disables the CCCD and releases the stream.

## Local GATT server

The optional server definition passed to `ble.open()` is:

```text
BLEGattServerDefinition = {
  services: {
    id: string,
    uuid: string,
    primary?: boolean,
    characteristics: {
      id: string,
      uuid: string,
      properties: ("broadcast" | "read" | "write" |
                   "write-without-response" | "notify" | "indicate" |
                   "authenticated-signed-write")[],
      permissions?: ("encrypted-read" | "encrypted-write" |
                     "authenticated-read" | "authenticated-write")[],
      maxLength: number,
      initialValue?: ByteSource,
      storeWrites?: boolean
    }[]
  }[]
}
```

Service and characteristic IDs are unique application identifiers. UUIDs use
standard 16-bit, 32-bit, or 128-bit text forms. `primary` and `storeWrites`
default to `true`; `maxLength` is positive and bounded by the compiled maximum
attribute size.

`adapter.server()` returns the configured `BLEGattServer` or `null`.

```text
server.status() -> {
  open, services, characteristics, activeConnections,
  writes, notifications, indications, droppedEvents
}
server.watch({ capacity?: number }?) -> EventQueue<BLEServerEvent>
server.characteristic(id) -> BLELocalCharacteristic
```

The server watch queue uses the capacity captured by `ble.open()`. It delivers:

```text
{
  type: "write", sequence, timestampUs, connection,
  characteristicId, offset, data: ByteView
}
{
  type: "subscription", sequence, timestampUs, connection,
  characteristicId, notify, indicate
}
```

Close a write event's `data` view after use. Local characteristics expose
read-only `id`, `uuid`, `maxLength`, and `properties`, plus:

```text
characteristic.value() -> ByteView
characteristic.setValue(data: ByteSource) -> number
characteristic.notify(data?: ByteSource, options?: {
  connection?: BLEConnection,
  indication?: boolean,
  timeoutMs?: number
}) -> {
  attemptedConnections: number,
  submittedConnections: number,
  bytes: number,
  indication: boolean
}
```

## Bonds, errors, and lifecycle

`adapter.bonds()` returns `{ peer, authenticated, secureConnections }[]`.
`adapter.removeBond(peer)` returns whether a bond was removed, and
`adapter.clearBonds()` returns the number removed.

Operational failures use `BLEError` with `operation:"ble"`, a stable `code`,
and `{ hostCode, attCode, connectionId, attributeHandle }` details. Stable codes
cover support/open state, stale adapters/connections/attributes/subscriptions,
GAP conflicts, timeout and queue limits, connection/disconnection, GATT and
security failures, expired pairing, payload/server limits, and closing state.

One-shot BLE operations work as cooperative direct calls and through
`Future.call(...)`. GAP start/stop operations share one lane, GATT procedures
use one lane per connection, and server indications use a confirmation lane.
`BLEAdapter.close()` stops GAP sources and connections, deinitializes NimBLE,
and invalidates the adapter generation after complete cleanup. Retained cleanup
state permits a later `close()` call to finish native deinitialization.

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


## Future timeout and native cleanup

A queued operation times out without native I/O. Started GATT/Pair/MTU/RSSI
operations may terminate their matching connection; connect timeout cancels that
attempt and terminates a late success. Scan/advertise stop only their owner.
Indication timeout affects only submitted recipients still awaiting confirmation.
`receive(timeoutMs)` remains a queue wait returning `null`.

Public timeout does not release native storage or permit lane reuse. GATT returns
`BLE_BUSY` until the original native operation terminates; unrelated connections
remain usable. Indication TX submission (`status=0`) is not peer confirmation.
See [operation-by-operation cleanup](ble-lifecycle.md) for native completion and
Host teardown boundaries. Timeout does not lower security or auto-reconnect.
