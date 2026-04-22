__esp32qjsTest.run("http/offline", function () {
  var headers = new Headers({ Foo: "Bar" });
  var request = new Request("https://example.com/test?x=1", {
    method: "POST",
    headers: headers,
    body: "payload",
  });
  var jsonRequest = new Request("https://example.com/json?ok=1", {
    method: "POST",
    headers: { "content-type": "application/json" },
    body: JSON.stringify({ ok: true }),
  });
  var clonedRequest = new Request(request);
  var response = Response.text("ok", {
    status: 201,
    headers: { "x-test": "1" },
  });
  var jsonResponse = Response.json({ value: 7 });
  var streamPath = "response-stream.txt";
  var streamResponse;
  var server;
  var fileHandler;
  var entries;

  __esp32qjsTest.equal(headers.get("foo"), "Bar", "headers should normalize names");
  __esp32qjsTest.ok(headers.has("foo"), "headers.has should find normalized key");
  headers.set("X-Test", "1");
  __esp32qjsTest.equal(headers.get("x-test"), "1", "headers.set should update values");
  entries = headers.entries();
  __esp32qjsTest.ok(entries.length >= 2, "headers.entries should include all values");
  __esp32qjsTest.equal(headers.toObject()["x-test"], "1", "headers.toObject should expose plain object");
  __esp32qjsTest.ok(headers.delete("x-test"), "headers.delete should report removal");
  __esp32qjsTest.ok(!headers.has("x-test"), "headers.delete should remove key");

  __esp32qjsTest.equal(request.method, "POST", "request method");
  __esp32qjsTest.equal(request.path, "/test", "request path");
  __esp32qjsTest.equal(request.queryString, "x=1", "request query string");
  __esp32qjsTest.equal(request.query.x, "1", "request query object");
  __esp32qjsTest.equal(request.text(), "payload", "request body text");
  __esp32qjsTest.equal(clonedRequest.url, request.url, "Request(Request) should clone url");
  __esp32qjsTest.equal(jsonRequest.json().ok, true, "request body json");

  __esp32qjsTest.equal(response.status, 201, "response status");
  __esp32qjsTest.ok(response.ok, "response ok for 2xx");
  __esp32qjsTest.equal(response.headers.get("x-test"), "1", "response headers");
  __esp32qjsTest.equal(response.text(), "ok", "response body text");
  __esp32qjsTest.equal(jsonResponse.json().value, 7, "response json body");
  __esp32qjsTest.equal(jsonResponse.headers.get("content-type"), "application/json; charset=utf-8", "response json content type");

  fs.writeText(streamPath, "stream-body");
  streamResponse = Response.stream(fs.open(streamPath, "r"), { status: 202 });
  __esp32qjsTest.equal(streamResponse.status, 202, "stream response status");
  __esp32qjsTest.equal(streamResponse.text(), "stream-body", "stream response body");
  fs.remove(streamPath);

  __esp32qjsTest.ok(typeof http.fetch === "function", "http.fetch should exist");
  __esp32qjsTest.ok(typeof http.server === "function", "http.server should exist");
  __esp32qjsTest.ok(typeof http.DEFAULT_TIMEOUT_MS === "number", "http timeout constant");
  __esp32qjsTest.ok(typeof staticFileHandler === "function", "staticFileHandler global should exist");

  server = http.server({ port: 8080 });
  __esp32qjsTest.ok(typeof server.get === "function", "server.get should exist");
  __esp32qjsTest.ok(typeof server.post === "function", "server.post should exist");
  __esp32qjsTest.ok(typeof server.start === "function", "server.start should exist");
  __esp32qjsTest.ok(typeof server.stop === "function", "server.stop should exist");
  server.get("/ping", function () {
    return Response.text("pong");
  });

  fileHandler = staticFileHandler(".");
  __esp32qjsTest.ok(fileHandler && typeof fileHandler.handle === "function", "static file handler should expose handle()");

  return { method: request.method, status: response.status };
});
