# `http` Module

The namespace is present when the HTTP client or server feature is enabled.

- `http.DEFAULT_TIMEOUT_MS` / `http.MAX_BODY_BYTES`
  HTTP-client limits when `sys.info.features.http` is enabled.
- `http.fetch(input, options?)`
  Run one request through the native HTTP Future driver. Options include
  `method`, `headers`, UTF-8 string, `Stream`, `ByteView`, or `ByteSpanSource`
  `body`, `timeoutMs`, and `maxBodyBytes`. Before the HTTP worker starts,
  binary input is materialized into a length-exact native PSRAM-first buffer,
  preserving embedded NUL bytes and enforcing a 1 MiB request-body default
  limit. A source is closed on all terminal paths. Native client cleanup is part
  of the request result: cleanup failure discards an otherwise successful
  response, retains the client handle in a bounded pending-cleanup list, and
  keeps its Future capacity occupied until a later request or runtime teardown
  completes the retry.

The HTTP client remains available without TLS for `http://` URLs. An
`https://` URL is rejected before its worker starts when
`sys.info.features.tls` is false; there is no insecure fallback.

Call `sys.time.sync(...)` once after a network interface connects and before
public HTTPS.
HTTPS uses the same TLS verification and structured error categories described
in the socket section; it has no insecure or skip-verification option.
- `http.server(options?)`
  Create a low-level declarative server when
  `sys.info.features.httpServer` is enabled.

```js
var left = Future.call(http.fetch, http, ["https://example.com/a"]);
var right = Future.call(http.fetch, http, ["https://example.com/b"]);
var responses = Future.all([left, right]).wait(10000);
print(responses[0].status, responses[1].status);
```

### HTTP Server API

`http.server(options?)` returns a generation-checked `HttpServer` object.

- `server.route(method, path)`
  Register a declarative route. Supported methods are `GET`, `POST`, `PUT`,
  `PATCH`, `DELETE`, `HEAD`, `OPTIONS`, and `ANY`; `*` in a path uses the
  built-in glob matcher. Returns `true` after registration.
- `server.start()` / `server.stop()`
  Start or stop listening while retaining the server and route table. Each
  returns `true` only when it changed the listening state and `false` when the
  requested state was already active.
  If the native driver rejects stop, the method throws and preserves the native
  handle, listening state, and route-registration state for a later retry.
- `server.receive(timeoutMs?)`
  Return the next matching `Request`, or `null` at the timeout. The request
  queue is bounded and rejects overflow with HTTP 503.
- `server.stats()`
  Return the same bounded queue statistics as `EventQueue.stats()`, including
  queued requests and the exact dropped count.
- `server.respond(request, response)`
  Complete one live request with a `Response`. A request is generation-checked
  and cannot be completed twice. This method has a native Future driver, so
  `Future.call(server.respond, server, [request, response])` returns before a
  slow response stream or socket finishes.
- `server.removeRoute(path, method?)` / `server.clearRoutes()`
  Remove matching declarative routes.
- `server.close()`
  Stop the listener, close its EventQueue, reject pending requests, and release
  the native slot. Repeated close is safe. A native stop failure is reported
  before the EventQueue, routes, requests, or JavaScript handle are detached;
  `close()` remains available as a cleanup retry, and runtime initialization
  retries any orphaned native handle before constructing a new state.

Request bodies larger than 8192 bytes are rejected with HTTP 413 before they
enter the queue. `request.bytes(maxBytes?)` preserves binary input as an owned
`ByteView`. `Response.stream(...)` and `Response.bytes(...)` write in chunks
and close the stream/source after the response is sent, including failure and
cancellation paths. Known-length bodies set `Content-Length`; an explicitly
mismatched length is rejected before dispatch. Binary bodies do not receive an
implicit `Content-Type`, while existing string-body defaults remain unchanged.

```js
var server = http.server({ port: 8080, host: "0.0.0.0" });
server.route("GET", "/ping");
server.route("POST", "/echo");
server.start();

var request = server.receive(1000);
if (request !== null) {
  if (request.method === "POST") {
    server.respond(request, Response.text(request.text()));
  } else {
    server.respond(request, Response.text("pong"));
  }
}
```
