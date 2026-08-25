test("http/network", function () {
  var cfg = test.requireConfig("wifiSsid", "wifiPassword", "httpUrl");
  var wifiStatus = wifi.status();
  var response;
  var syncResponse;
  var localServer;
  var localUrl;
  var bodyLimitError;
  var syncBodyLimitError = "";
  var boundedRequest;
  var incomingRequest;
  var cancelledRequest;
  var replacementRequest;
  var replacementResponse;
  var body;

  if (!wifiStatus.connected) {
    wifiStatus = Future.call(wifi.connect, wifi,
      [cfg.wifiSsid, cfg.wifiPassword, 15000]).wait(20000);
  }

  test.ok(wifiStatus.connected, "wifi should be connected before fetch");

  test.equal(wifi.syncTime({
    servers: ["pool.ntp.org", "time.cloudflare.com"],
    timeoutMs: 15000
  }).synchronized, true, "time should be synchronized before public fetch");

  response = Future.call(fetch, globalThis, [cfg.httpUrl]).wait(20000);

  test.ok(response.status >= 200 && response.status < 600, "fetch status should be valid");
  test.equal(typeof response.text, "function", "fetch response should expose text()");
  body = response.text();
  test.ok(typeof body === "string", "fetch body should be text");

  test.equal(typeof http.fetch, "function", "http.fetch should be available");
  syncResponse = http.fetch(cfg.httpUrl, { timeoutMs: 15000 });
  test.ok(syncResponse.status >= 200 && syncResponse.status < 600,
    "synchronous fetch worker status should be valid");
  test.equal(typeof syncResponse.text, "function",
    "synchronous fetch response should expose text()");
  test.ok(typeof syncResponse.text() === "string",
    "synchronous fetch worker body should be text");

  if (body.length > 1) {
    try {
      http.fetch(cfg.httpUrl, { timeoutMs: 15000, maxBodyBytes: 1 });
    } catch (syncBodyLimitFailure) {
      syncBodyLimitError = syncBodyLimitFailure && syncBodyLimitFailure.message
        ? syncBodyLimitFailure.message
        : String(syncBodyLimitFailure);
    }
    test.ok(syncBodyLimitError.indexOf("maxBodyBytes") >= 0,
      "synchronous response capture should enforce maxBodyBytes");
  }

  test.equal(typeof http.server, "function", "http.server should be available");
  localServer = http.server({ port: 18081, host: "0.0.0.0" });
  test.equal(typeof localServer.route, "function", "HTTP server should expose route()");
  test.equal(typeof localServer.receive, "function", "HTTP server should expose receive()");
  test.equal(typeof localServer.respond, "function", "HTTP server should expose respond()");
  test.equal(typeof localServer.start, "function", "HTTP server should expose start()");
  test.equal(typeof localServer.close, "function", "HTTP server should expose close()");
  localServer.route("GET", "/bounded");
  localServer.start();
  localUrl = "http://" + wifiStatus.ip + ":18081/bounded";

  boundedRequest = Future.call(fetch, globalThis, [localUrl, { maxBodyBytes: 8 }]);
  incomingRequest = localServer.receive(5000);
  test.ok(incomingRequest !== null, "bounded request should reach the local server");
  localServer.respond(incomingRequest, Response.text("0123456789abcdef"));
  try {
    boundedRequest.wait(5000);
    bodyLimitError = "bounded fetch unexpectedly succeeded";
  } catch (error) {
    bodyLimitError = String(error);
  }
  test.ok(bodyLimitError.indexOf("maxBodyBytes") >= 0,
    "response capture should stop at the configured body limit");

  cancelledRequest = Future.call(fetch, globalThis, [localUrl]);
  test.ok(cancelledRequest.cancel(), "queued request Future should cancel");
  test.equal(cancelledRequest.status(), "cancelled", "cancel should settle request Future");
  test.ok(!cancelledRequest.cancel(), "cancel should be idempotent after settlement");

  replacementRequest = Future.call(fetch, globalThis, [localUrl]);
  incomingRequest = localServer.receive(5000);
  test.ok(incomingRequest !== null, "replacement request should reach the local server");
  localServer.respond(incomingRequest, Response.text("replacement"));
  replacementResponse = replacementRequest.wait(5000);
  test.equal(replacementResponse.status, 200, "replacement request should complete");

  localServer.close();
  wifi.disconnect();

  return { status: response.status, syncStatus: syncResponse.status, bodyLength: body.length };
});
