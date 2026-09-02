# `websocketClient` Module

`websocketClient` is exposed when `sys.info.features.websocket` is enabled.
The current ESP-IDF WS/WSS transport is selected together with the TLS
capability, so a WebSocket client build requires `sys.info.features.tls`.
It uses a bounded `EventQueue` handle and does not invoke application callbacks.

- `websocketClient.capabilities()` reports the target, ESP-IDF version,
  compile-time message limit, and `nativeReconnect: false`.
- `websocketClient.open(options)`
  Start a connection and return a handle. Required `options.url` uses `ws://`
  or `wss://`. Optional controls are `authorization`, `subprotocol`,
  `networkTimeoutMs` (1000..120000), `sendTimeoutMs` (0..5000),
  `pingIntervalSec` (1..3600), and `maxMessageBytes` (256 up to the firmware
  limit). Each native handle performs one connection attempt and never
  reconnects itself. WSS always verifies
  the public CA chain, hostname, and certificate dates; there is no option to
  disable verification.
- `handle.receive(timeoutMs?)`
  Return the next `{ type: "open" | "message" | "close" | "error", ... }`
  event, or `null` at the timeout. Every event carries `sequence` and
  `timestampUs`; message `data` is a string for text frames and an owning
  `ByteView` for binary frames.
- `handle.send(data)` / `handle.status()` / `handle.stats()` / `handle.close()`
  Send text or bounded binary `ByteSource`/`ByteSpanSource` data, inspect
  counters, or close the event source. The send operation has
  a native Future driver; use `Future.call(handle.send, handle, [text])` to
  return before network backpressure clears. Only one send may be active.
  Closing marks the handle closed immediately and moves the potentially
  blocking ESP-IDF stop/destroy work to the shared native worker pool. It does
  not wait for ESP-IDF's connection task. While cleanup is pending,
  `status().closing` is `true` and a new `open()` is rejected; once it becomes
  `false`, the client is fully released and may be opened again. Stop,
  event-unregister, or destroy failure retains the exact remaining native
  suffix and keeps `closing` true; another `close()` or runtime teardown retries
  it. The first close returns `true`; ordinary repeated closes return `false`.
  Reconnect timing, endpoint fallback, credential refresh, and retry limits
  belong to a JavaScript policy library.

```js
var client = websocketClient.open({
  url: "wss://agent.example/ws",
  authorization: "Bearer paired-device-token"
});
var event = client.receive(10000);
if (event && event.type === "open") {
  client.send(JSON.stringify({ type: "protocol.ping", id: 1 }));
}
```

Complete text and binary messages are accepted. Close, ping, and pong control
frames are handled natively and are not reported as application errors. A
dropped binary event releases its native payload automatically.
