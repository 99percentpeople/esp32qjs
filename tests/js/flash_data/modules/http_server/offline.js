test("http_server/offline", function () {
  var features = sys.info().features;
  var server;
  var replacementServer;
  var staleServerError = "";
  var fileHandler;

  test.ok(typeof http === "object" && http !== null, "http namespace should exist");
  test.ok(typeof http.server === "function", "http.server should exist");

  server = http.server({ port: 8080 });
  test.ok(typeof server.get === "function", "server.get should exist");
  test.ok(typeof server.post === "function", "server.post should exist");
  test.ok(typeof server.start === "function", "server.start should exist");
  test.ok(typeof server.stop === "function", "server.stop should exist");
  test.ok(typeof server.close === "function", "server.close should exist");
  test.ok(typeof server.removeRoute === "function", "server.removeRoute should exist");
  test.ok(typeof server.clearRoutes === "function", "server.clearRoutes should exist");
  server.get("/ping", function () {
    return Response.text("pong");
  });
  server.get(/^\/items\/.+$/, {
    handle: function () {
      return Response.json({ ok: true });
    },
  });

  if (features.staticFileHandler) {
    test.ok(typeof http.staticFileHandler === "function",
      "http.staticFileHandler should exist when the composite feature is enabled");
    test.ok(typeof staticFileHandler === "function",
      "global staticFileHandler should exist when the composite feature is enabled");
    fileHandler = staticFileHandler(".");
    test.ok(fileHandler && typeof fileHandler.handle === "function",
      "static file handler should expose handle()");
    var missingResponse = fileHandler.handle({ relativePath: "missing-gc-root-fixture.txt" });
    var fontResponse = fileHandler.handle({ relativePath: "_sys/display/fonts/mono5x7.eqf" });
    test.equal(missingResponse.status, 404,
      "static file handler should return a rooted 404 response");
    test.equal(missingResponse.text(), "Not Found",
      "static file handler should preserve the 404 body");
    test.equal(fontResponse.status, 200,
      "static file handler should return a rooted file response");
    test.ok(fontResponse.text().length > 0,
      "static file handler should preserve the file stream");
  } else {
    test.equal(typeof http.staticFileHandler, "undefined",
      "http.staticFileHandler should stay hidden without fs support");
    test.equal(typeof staticFileHandler, "undefined",
      "global staticFileHandler should stay hidden without fs support");
  }

  test.equal(server.removeRoute("/ping", "GET"), 1,
    "removeRoute should release a matching method route");
  test.equal(server.removeRoute("/ping", "GET"), 0,
    "removeRoute should ignore an already removed route");
  test.equal(server.removeRoute(/^\/items\/.+$/), 1,
    "removeRoute should match equivalent RegExp routes");

  server.get("/one", function () { return Response.text("one"); });
  server.post("/two", function () { return Response.text("two"); });
  test.equal(server.clearRoutes(), 2, "clearRoutes should release every server route");
  test.equal(server.clearRoutes(), 0, "clearRoutes should be idempotent");

  server.close();
  server.close();
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
    staticFileHandler: features.staticFileHandler,
  };
});
