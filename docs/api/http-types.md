# `Headers`, `Request`, and `Response`

- `new Headers(init?)`
  Create a header collection from a plain object or another `Headers`.
- `new Request(input, init?)`
  Create a request from a URL string or another `Request`.
- `new Response(body?, init?)`
  Create a response from a string, `Stream`, `ByteView`, `ByteSpanSource`, or
  omitted body.
- `Response.text(text, init?)`
- `Response.json(value, init?)`
- `Response.stream(stream, init?)`
- `Response.bytes(body, init?)`
  Create a response from a `ByteView` or `ByteSpanSource` without UTF-8
  conversion.

`Headers` methods:

- `headers.get(name)`
- `headers.set(name, value)`
- `headers.has(name)`
- `headers.delete(name)`
- `headers.entries()`
- `headers.toObject()`

`Request` shape:

- `method`
- `url`
- `path`
- `queryString`
- `query`
- `route`
  The matched route pattern as a string.
- `mountPath`
  The mounted path prefix computed by the router, primarily for middleware.
- `relativePath`
  The path after removing the mounted prefix, primarily for file-serving middleware.
- `headers`
  A `Headers` object.
- `body`
  A `Stream`.
- `text()`
- `bytes(maxBytes?)`
- `json()`

`Response` shape:

- `ok`
- `status`
- `statusText`
- `url`
- `headers`
  A `Headers` object.
- `body`
  A `Stream`.
- `text()`
- `bytes(maxBytes?)`
- `json()`

Body consumption notes:

- `Request.text()` / `Request.bytes()` / `Request.json()` read the full request
  body and consume the underlying stream. `bytes()` returns an owned
  `ByteView` and enforces its optional bound.
- `Response.text()` / `Response.bytes()` / `Response.json()` do the same for a
  response body.
- If you read directly from `request.body` or `response.body`, close the stream manually when you are done.

Examples:

```js
var request = new Request("https://example.com", {
  method: "POST",
  headers: { "content-type": "application/json" },
  body: JSON.stringify({ hello: "world" }),
});

var response = http.fetch(request);
print(response.status, response.ok);
print(response.text().length);

var headers = new Headers({ "content-type": "text/plain; charset=utf-8" });
print(headers.get("content-type"));
```
