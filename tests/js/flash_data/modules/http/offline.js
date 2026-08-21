test("http/offline", function () {
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
  var entries;
  var globalFetchError = "";
  var moduleFetchError = "";
  var maxBodyError = "";
  var embeddedRequest;
  var embeddedBytes;
  var binaryResponse;
  var binaryBytes;
  var binaryConstructor;
  var binaryConstructorBytes;
  var boundedBytesError = "";
  var contentLengthError = "";

  test.equal(headers.get("foo"), "Bar", "headers should normalize names");
  test.ok(headers.has("foo"), "headers.has should find normalized key");
  headers.set("X-Test", "1");
  test.equal(headers.get("x-test"), "1", "headers.set should update values");
  entries = headers.entries();
  test.ok(entries.length >= 2, "headers.entries should include all values");
  test.equal(headers.toObject()["x-test"], "1", "headers.toObject should expose plain object");
  test.ok(headers.delete("x-test"), "headers.delete should report removal");
  test.ok(!headers.has("x-test"), "headers.delete should remove key");

  test.equal(request.method, "POST", "request method");
  test.equal(request.path, "/test", "request path");
  test.equal(request.queryString, "x=1", "request query string");
  test.equal(request.query.x, "1", "request query object");
  test.equal(request.text(), "payload", "request body text");
  test.equal(clonedRequest.url, request.url, "Request(Request) should clone url");
  test.equal(jsonRequest.json().ok, true, "request body json");

  test.equal(response.status, 201, "response status");
  test.ok(response.ok, "response ok for 2xx");
  test.equal(response.headers.get("x-test"), "1", "response headers");
  test.equal(response.text(), "ok", "response body text");
  test.equal(jsonResponse.json().value, 7, "response json body");
  test.equal(jsonResponse.headers.get("content-type"), "application/json; charset=utf-8", "response json content type");

  embeddedRequest = new Request("https://example.com/binary", {
    method: "POST",
    body: "A\x00B"
  });
  embeddedBytes = embeddedRequest.bytes(3);
  test.equal(embeddedBytes.byteLength, 3,
    "Request.bytes should preserve embedded NUL length");
  test.equal(embeddedBytes.toArray()[1], 0,
    "Request.bytes should preserve embedded NUL value");
  binaryResponse = Response.bytes(embeddedBytes, { status: 206 });
  test.equal(binaryResponse.headers.get("content-type"), null,
    "binary response should not infer Content-Type");
  binaryBytes = binaryResponse.bytes(3);
  test.equal(binaryBytes.toArray()[2], 0x42,
    "Response.bytes should return an owned exact binary body");
  binaryConstructor = new Response(embeddedBytes);
  binaryConstructorBytes = binaryConstructor.bytes();
  test.equal(binaryConstructorBytes.byteLength, 3,
    "Response constructor should accept ByteView");

  try {
    Response.text("four").bytes(3);
  } catch (boundedFailure) {
    boundedBytesError = String(boundedFailure);
  }
  test.ok(boundedBytesError.indexOf("exceeds") >= 0,
    "bytes(maxBytes) should reject an oversized body");

  try {
    fetch("http://127.0.0.1:9/binary", {
      method: "POST",
      headers: { "content-length": "2" },
      body: embeddedBytes,
      timeoutMs: 1
    });
  } catch (lengthFailure) {
    contentLengthError = String(lengthFailure);
  }
  test.ok(contentLengthError.indexOf("Content-Length") >= 0,
    "fetch should reject a mismatched binary Content-Length before dispatch");

  fs.writeText(streamPath, "stream-body");
  streamResponse = Response.stream(fs.open(streamPath, "r"), { status: 202 });
  test.equal(streamResponse.status, 202, "stream response status");
  test.equal(streamResponse.text(), "stream-body", "stream response body");
  fs.remove(streamPath);

  test.ok(typeof http.fetch === "function", "http.fetch should exist");
  test.ok(typeof http.fetchAsync === "undefined", "http.fetchAsync should not exist");
  test.ok(typeof http.async === "undefined", "http.async should be removed");
  test.ok(typeof http.DEFAULT_TIMEOUT_MS === "number", "http timeout constant");
  test.ok(typeof http.MAX_BODY_BYTES === "number" && http.MAX_BODY_BYTES > 0,
    "http response body limit constant");

  try {
    fetch("https://example.com", function () {});
  } catch (globalFailure) {
    globalFetchError = globalFailure && globalFailure.message ? globalFailure.message : String(globalFailure);
  }
  test.ok(globalFetchError.indexOf("Future.call") >= 0, "global fetch should reject callback overloads");

  try {
    http.fetch("https://example.com", function () {});
  } catch (moduleFailure) {
    moduleFetchError = moduleFailure && moduleFailure.message ? moduleFailure.message : String(moduleFailure);
  }
  test.ok(moduleFetchError.indexOf("Future.call") >= 0, "http.fetch should reject callback overloads");

  try {
    http.fetch("https://example.com", { maxBodyBytes: 0 });
  } catch (maxBodyFailure) {
    maxBodyError = maxBodyFailure && maxBodyFailure.message
      ? maxBodyFailure.message
      : String(maxBodyFailure);
  }
  test.ok(maxBodyError.indexOf("maxBodyBytes") >= 0,
    "fetch should reject a non-positive response body limit before starting a worker");

  test.equal(binaryConstructorBytes.close(), true,
    "Response constructor ByteView should close explicitly");
  test.equal(binaryBytes.close(), true,
    "Response.bytes ByteView should close explicitly");
  test.equal(embeddedBytes.close(), true,
    "Request.bytes ByteView should close explicitly");
  test.equal(embeddedBytes.close(), true,
    "ByteView close should be idempotent");

  return { method: request.method, status: response.status };
});
