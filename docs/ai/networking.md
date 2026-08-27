# Networking API

## Network interfaces

- `net.status()` returns transport-neutral readiness, the primary interface,
  and bounded IPv4/IPv6 snapshots for every registered ESP-NETIF interface.
- `net.watch()` returns an `EventQueue` whose first item is the current status
  and whose later items contain convergent status snapshots after IP or route
  changes. Close each watcher explicitly.

`status.ready` means at least one interface is up with an address. It is not a
DNS, Internet, or service reachability probe. Wi-Fi, Ethernet, PPP, and custom
link modules remain responsible for creating and controlling their interfaces.

## Wi-Fi

- `wifi.status()`
- `wifi.scan()`
- `wifi.connect(ssid, password, timeoutMs?)`
- `wifi.disconnect()`

Operations are synchronous and bounded by the active deadline. Do not expose passwords in returned results or logs.
Only workspace code owns these operations. The Agent connectivity observer
reads status but never calls `wifi.scan`, `wifi.connect`, or `wifi.disconnect`.
A successful workspace connection automatically makes the paired remote RPC
channel eligible to open; Web Serial/USB remains usable before and during it.
Do not overlap connection attempts. After a returned failure or timeout, a new
bounded `wifi.connect` call is safe.

After connecting, synchronize time before public HTTPS, TLS, or WSS. The
workspace supplies one to four server names. Direct `sys.time.sync` waits
cooperatively without blocking the runtime. Optional `timeoutMs` must be an
integer from 1 through 60000 and defaults to 15000;
`Future.call(sys.time.sync, sys.time, [options])` provides an independently
cancellable handle. Do not overlap calls: another call while SNTP is active is
rejected with `error.code === "TIME_SYNC_BUSY"`, never merged or queued. Later
calls return immediately once the clock is valid. `sys.time.status()` reports
`{ synchronized, synchronizing, unixTimeMs }`. SNTP is ordinary clock setup and
does not defend against active network time tampering.

```js
(function () {
    var networks = wifi.scan();
    return networks.slice(0, 10);
})()
```

```js
(function () {
    var status = wifi.connect("example", "password", 15000);
    var clock = sys.time.sync({
        servers: ["pool.ntp.org"],
        timeoutMs: 10000
    });
    return {
        connected: status.connected,
        ip: status.ip,
        unixTimeMs: clock.unixTimeMs
    };
})()
```

For reboot startup, keep policy in an idempotent workspace module loaded by
`index.js`. Read application settings from registered `sys.config(key)` values
or an application-owned NVS namespace; inspect `nvs.status().encrypted` before
putting a password in NVS. Never place credentials in returned values.

## HTTP client and body types

Check `sys.info.features.tls` before selecting an `https://` URL. A build
without TLS keeps `http://` support and rejects HTTPS before starting a worker;
there is no insecure fallback.

Complete `sys.time.sync(...)` after network connection before using a public
`https://` URL. Certificate authority, DNS name, and validity dates are always
verified; there is no insecure option.

- `fetch(input, options?)` and `http.fetch(input, options?)` return a `Response`.
- `new Headers(init?)`
- `new Request(input, init?)`
- `new Response(body?, init?)`
- `Response.text(text, init?)`
- `Response.json(value, init?)`
- `Response.stream(stream, init?)`
- `Response.bytes(body, init?)`

Request options are `method`, `headers`, a UTF-8 string, `Stream`, `ByteView`,
or `ByteSpanSource` `body`, `timeoutMs`, and `maxBodyBytes`. Binary bodies
preserve embedded NUL and do not set `Content-Type` implicitly. A known body
length sets `Content-Length`; an explicit mismatched value is rejected before
dispatch. The client materializes a source into a bounded native buffer before
its worker starts, with a 1 MiB request-body limit by default.

Response fields and methods are `status`, `statusText`, `headers`, `ok`, `url`,
`body`, `text()`, `bytes()`, and `json()`. All three helpers consume the body;
`bytes(maxBytes?)` returns an owned `ByteView`; close that view after the last
consumer or `toArray()` conversion. A `Response` has no `close()` method; close
`response.body` only when reading the stream directly instead of using a body
helper.

```js
(function () {
    var response = fetch("https://example.com/status", {
        method: "GET",
        headers: { "accept": "application/json" },
        timeoutMs: 10000,
        maxBodyBytes: 4096
    });
    return {
        status: response.status,
        body: response.json()
    };
})()
```

For independent calls:

```js
(function () {
    var left = Future.call(fetch, this, ["https://example.com/a"]);
    var right = Future.call(fetch, this, ["https://example.com/b"]);
    var responses = Future.all([left, right]).wait(10000);
    return [responses[0].status, responses[1].status];
})()
```

## HTTP server

```js
var server = http.server({ port: 8080, host: "0.0.0.0" });
server.route("GET", "/health");
server.start();
```

- `route(method, path)` registers a bounded route and returns `true`.
- `start()` / `stop()` return `true` only when the listening state changes.
- `receive(timeoutMs?)` returns a `Request` or `null`.
- `stats()` reports the bounded request EventQueue, including dropped requests.
- `respond(request, response)` completes a live request. Use
  `Future.call(server.respond, server, [request, response])` when the response
  stream should progress without holding the current JS call.
- `close()` releases the server and its event queue.

The server is a receive/respond loop, not a callback router:

```js
(function () {
    var server = http.server({ port: 8080 });
    try {
        server.route("GET", "/health");
        server.start();
        var request = server.receive(5000);
        if (request) {
            server.respond(request, Response.json({ ok: true }));
        }
        return { served: !!request };
    } finally {
        server.close();
    }
})()
```

Binary echo without UTF-8 conversion:

```js
(function () {
    var server = http.server({ port: 8080 });
    try {
        server.route("POST", "/echo");
        server.start();
        var request = server.receive(5000);
        if (!request) return false;
        var bytes = request.bytes(8192);
        return server.respond(request, Response.bytes(bytes));
    } finally {
        server.close();
    }
})()
```

`Response.bytes(frame.source())` streams a camera framebuffer as spans. The
server closes the source on all terminal paths; see `doc://framework/media` for the
frame lease rules.

## Raw sockets

- `socket.openTCP(options?)` returns a `TCPSocket`.
- `socket.listenTCP({ localPort, backlog? })` returns a `TCPListener`.
- `socket.openUDP(options?)` returns a `UDPSocket`.
- `localPort` defaults to `0` for client TCP and UDP objects.
- `tls: true` is valid only for outbound TCP clients. The TLS handshake is part
  of `TCPSocket.connect`, uses the system CA bundle, and verifies the peer for
  the supplied host and current certificate dates. TLS is not supported for
  UDP, listeners, or accepted sockets.
- A build with `sys.info.features.tls === false` rejects `tls: true` before
  allocating the socket object; plaintext TCP and UDP remain available.
- `tcp.connect(host, port, { timeoutMs? })`
- `tcp.send(bytesOrSource, timeoutMs?)` / `tcp.recv(maxBytes?, timeoutMs?)`
- `listener.accept(timeoutMs?)`
- `udp.sendTo(host, port, bytes)` / `udp.receiveFrom(maxBytes?, timeoutMs?)`
- Every socket object has `status()` and `close()`. Status includes `secure` but
  intentionally exposes no internal numeric id.
- `socket.MAX_TRANSFER_BYTES` is the per-call chunk/datagram limit.

TCP send accepts a native `ByteView`, array-like byte source, or one-shot
`ByteSpanSource`. Sources are consumed span by span across partial socket and
TLS writes and are closed on success, failure, cancellation, or timeout. TCP
receive data is a `ByteView` or `null`; UDP returns
`{data, remoteHost, remotePort}` with `data` as a `ByteView`. Call `toArray()`
only when a JavaScript copy is needed, then close the view. TCP receives are
chunks, not application messages.

```js
(function () {
    var client = socket.openTCP({ tls: true });
    var bytes = null;
    var data = null;
    try {
        client.connect("example.com", 443, { timeoutMs: 10000 });
        var payload = fs.open("request.bin", "rb");
        bytes = payload.read(4096);
        payload.close();
        client.send(bytes, 1000);
        data = client.recv(1024, 1000);
        return {
            secure: client.status().secure,
            data: data === null ? null : data.toArray()
        };
    } finally {
        if (data !== null) data.close();
        if (bytes !== null) bytes.close();
        client.close();
    }
})()
```

Never close an Agent-owned transport. Create and retain a fresh socket object,
then close that same object.

HTTPS and raw TLS failures expose `code` as `TLS_ALLOC_FAILED`,
`TLS_TIME_INVALID`, `TLS_VERIFY_FAILED`, `TLS_HANDSHAKE_FAILED`, or
`TLS_TIMEOUT`, with numeric `espTlsError`, `mbedtlsError`, and `verifyFlags`.
For framework-built firmware, `sys.info.features.tls === true` means the public
CA bundle is present. `verifyFlags === 0x8` is the mbedTLS `NOT_TRUSTED` result,
but by itself does not distinguish a missing root from a chain or bundle
integration failure; inspect the generated profile and test another public-CA
host before assigning the cause.
Do not log certificate material or secrets. For repeated allocation failures,
compare `sys.status.memory.internal`, `.dma`, and `.psram` maximum contiguous
blocks and low-water marks before and after the operation; do not infer TLS or
DMA headroom from total `sys.freeHeap()`.

## WebSocket client

`websocketClient.open(options)` returns a handle.

Options include required `url` plus optional `authorization`, `subprotocol`,
`autoReconnect`, `useCertBundle`, `reconnectMs`, `networkTimeoutMs`,
`sendTimeoutMs`, `pingIntervalSec`, and `maxMessageBytes`. At least one network
interface must be ready. The runtime permits one WebSocket client at a time.
For `wss://`, synchronize time first with `sys.time.sync(...)`.

- `send(textOrBytes)` accepts a string, `ByteSource`, or `ByteSpanSource`.
- `receive(timeoutMs?)`
- `status()`
- `close()`

Events are consumed from a bounded `EventQueue`; no application callback is invoked. Close the handle in `finally`.
Event types are `open`, `message`, `close`, and `error`. Message events carry
UTF-8 string `data` for text frames or an owned `ByteView` for binary frames;
close/error events carry `code`, `message`, and `reconnecting`. Every event has
`sequence` and `timestampUs`.

```js
(function () {
    var ws = websocketClient.open({ url: "wss://example.com/events" });
    try {
        var event = ws.receive(5000);
        if (event && event.type === "open") ws.send("ping");
        return event;
    } finally {
        ws.close();
    }
})()
```
