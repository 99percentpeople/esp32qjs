# `socket` Module

`socket` is exposed when `sys.info.features.socket` is enabled. It provides
bounded TCP/UDP socket objects. Verified outbound TLS streams are
available only when the separately selectable `sys.info.features.tls` build
capability is enabled. The framework does not add line framing, reconnect
policy, authentication, or an application protocol.

- `socket.openTCP(options = {})`
  Return a `TCPSocket`. `options.localPort` binds the local port. For an
  outbound verified TLS client, use `socket.openTCP({ tls: true })`; TLS uses
  the system CA certificate
  bundle, verifies the DNS name and certificate validity dates, and does not
  support a fixed local port. When the TLS capability is omitted, requesting
  `tls: true` fails before allocating a socket object.
- `socket.listenTCP({ localPort, backlog = 4 })`
  Bind and return a `TCPListener`.
- `socket.openUDP(options = {})`
  Bind and return a `UDPSocket`; `options.localPort` defaults to `0`.
- `socket.MAX_TRANSFER_BYTES`
  Is the maximum bytes accepted by one datagram or stream chunk. TCP itself
  has no message boundary.
- `tcp.connect(remoteHost, remotePort, { timeoutMs = 5000 } = {})`
  Connect a `TCPSocket`. The host string may be an IP address or DNS name. DNS
  resolution is dispatched through the asynchronous lwIP resolver, so a slow
  lookup does not stop JavaScript, timers, other Futures, or EventQueues. The
  timeout covers resolution, TCP connect, and the TLS handshake when enabled.
  TLS connection setup runs in a bounded worker so ESP-IDF network waits do not
  block the JavaScript task; PSRAM profiles prefer external RAM for its stack.
- `listener.accept(timeoutMs = 0)`
  Return a connected `TCPSocket` or `null` when no connection is ready.
- `tcp.send(data, timeoutMs = 0)`
  Send a `ByteView`, array-like byte source, or `ByteSpanSource` and return the
  number of bytes written. A source is consumed one span at a time across
  partial plain/TLS writes, then closed on every terminal path. Source bodies
  are bounded by `CONFIG_ESP32_MQUICKJS_SOCKET_MAX_SOURCE_BYTES` (1 MiB by
  default) without requiring a single contiguous copy.
- `tcp.recv(maxBytes = socket.MAX_TRANSFER_BYTES, timeoutMs = 0)`
  Return one raw stream chunk or `null`. TCP has no message boundaries.
- `udp.sendTo(remoteHost, remotePort, data)`
  Send one UDP datagram. DNS names use the same asynchronous resolver path.
- `udp.receiveFrom(maxBytes = socket.MAX_TRANSFER_BYTES, timeoutMs = 0)`
  Return `{ data, remoteHost, remotePort }` or `null`.
- `socketObject.status()`
  Return protocol, local/remote endpoint, connected/listening state, peer-close
  state, and byte counters. Internal IDs are never exposed.
- `socketObject.close()`
  Request cancellation of pending operations and release the socket after each
  driver confirms completion. The first call returns `true`; repeated calls
  return `false`. A finalizer performs the same release when needed.

`accept`, `recv`, and `receiveFrom` default to non-blocking operation. Their
optional timeout is bounded to 60000 ms and remains subordinate to an outer
`sys.withTimeout()` deadline.

```js
var client = socket.openTCP({ localPort: 0 });
client.connect("192.0.2.10", 9000, { timeoutMs: 5000 });
var payload = fs.open("payload.bin", "rb");
var payloadBytes = payload.read(1024);
payload.close();
client.send(payloadBytes, 1000);
print(client.recv(1024, 100));
client.close();

var secureClient = socket.openTCP({ tls: true });
secureClient.connect("example.com", 443, { timeoutMs: 5000 });
secureClient.send(payloadBytes, 1000);
secureClient.close();

var udp = socket.openUDP({ localPort: 0 });
udp.sendTo("192.0.2.10", 9001, "hello");
print(JSON.stringify(udp.receiveFrom(1024, 100)));
udp.close();
```

Verified TLS failures from raw sockets and HTTPS fetches carry a stable `code`
of `TLS_ALLOC_FAILED`, `TLS_TIME_INVALID`, `TLS_VERIFY_FAILED`,
`TLS_HANDSHAKE_FAILED`, or `TLS_TIMEOUT`. `details.operationError`,
`details.espTlsError`, `details.mbedtlsError`, and `details.verifyFlags` retain
the numeric diagnostics. Certificate contents and secrets are not included.
Close or cancel always releases the per-connection TLS context.
PSRAM profiles retain the standard 16 KiB RX and 4 KiB TX records while placing
mbedTLS allocations in external RAM; non-PSRAM profiles continue to use
internal memory. The full ESP-IDF certificate bundle accepts valid
cross-signed public-CA chains. Peer and intermediate certificate dates remain
verified; a bundle-generated trust anchor has no encoded validity dates and is
treated as the trusted public key it represents.
HTTPS, secure WebSocket connections, and raw TLS sockets all use this same
certificate-bundle verification path.
If external RAM encryption is not enabled, TLS session
material in PSRAM remains readable to an attacker with physical memory access.
