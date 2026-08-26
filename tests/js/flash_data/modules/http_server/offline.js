test("http_server/offline", function () {
  var server;
  var replacementServer;
  var staleServerError = "";

  test.ok(typeof http === "object" && http !== null, "http namespace should exist");
  test.ok(typeof http.server === "function", "http.server should exist");

  server = http.server({ port: 8080 });
  test.ok(typeof server.route === "function", "server.route should exist");
  test.ok(typeof server.receive === "function", "server.receive should exist");
  test.ok(typeof server.respond === "function", "server.respond should exist");
  test.ok(typeof server.stats === "function", "server.stats should expose EventQueue statistics");
  test.equal(typeof server.get, "undefined", "callback route helpers should be removed");
  test.equal(typeof server.post, "undefined", "callback route helpers should be removed");
  test.ok(typeof server.start === "function", "server.start should exist");
  test.ok(typeof server.stop === "function", "server.stop should exist");
  test.ok(typeof server.close === "function", "server.close should exist");
  test.ok(typeof server.removeRoute === "function", "server.removeRoute should exist");
  test.ok(typeof server.clearRoutes === "function", "server.clearRoutes should exist");
  test.equal(typeof server.serverId, "undefined",
    "HTTP server must not expose an internal numeric id");
  test.equal(typeof server.serverGeneration, "undefined",
    "HTTP server must not expose its internal generation");
  test.equal(typeof http.staticFileHandler, "undefined",
    "callback-oriented static file handlers should be removed");
  test.equal(typeof staticFileHandler, "undefined",
    "global callback-oriented static file handlers should be removed");
  test.equal(server.route("GET", "/ping"), true,
    "route should report successful registration");
  test.equal(server.route("GET", "/items/*"), true,
    "route should register wildcard paths");
  test.equal(server.start(), true, "first start should transition the server");
  test.equal(server.start(), false, "repeated start should be idempotent");
  test.ok(server.started, "server.start should work before Wi-Fi initialization");
  test.equal(server.stop(), true, "first stop should transition the server");
  test.equal(server.stop(), false, "repeated stop should be idempotent");
  test.ok(!server.started, "server.stop should update the server state");
  test.equal(server.receive(0), null, "receive should return null when no request is queued");
  test.equal(server.stats().queued, 0, "request EventQueue should start empty");

  test.equal(server.removeRoute("/ping", "GET"), 1,
    "removeRoute should release a matching method route");
  test.equal(server.removeRoute("/ping", "GET"), 0,
    "removeRoute should ignore an already removed route");
  test.equal(server.removeRoute("/items/*"), 1,
    "removeRoute should match wildcard routes");

  server.route("GET", "/one");
  server.route("POST", "/two");
  test.equal(server.clearRoutes(), 2, "clearRoutes should release every server route");
  test.equal(server.clearRoutes(), 0, "clearRoutes should be idempotent");

  test.equal(server.close(), true, "first close should release the server");
  test.equal(server.close(), false, "repeated close should be idempotent");
  test.ok(server.closed, "close should mark the server closed");
  test.ok(!server.started, "close should stop the server");

  replacementServer = http.server({ port: 8080 });
  try {
    server.start();
  } catch (staleFailure) {
    staleServerError = staleFailure && staleFailure.message
      ? staleFailure.message
      : String(staleFailure);
  }
  test.ok(staleServerError.indexOf("closed or stale") >= 0,
    "a closed server object should not control a reused native slot");
  replacementServer.close();

  return {
    port: server.port,
    requestQueue: true,
  };
});
