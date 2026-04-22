test("http_server/offline", function () {
  var features = esp32.info().features;
  var server;
  var fileHandler;

  test.ok(typeof http === "object" && http !== null, "http namespace should exist");
  test.ok(typeof http.server === "function", "http.server should exist");

  server = http.server({ port: 8080 });
  test.ok(typeof server.get === "function", "server.get should exist");
  test.ok(typeof server.post === "function", "server.post should exist");
  test.ok(typeof server.start === "function", "server.start should exist");
  test.ok(typeof server.stop === "function", "server.stop should exist");
  server.get("/ping", function () {
    return Response.text("pong");
  });

  if (features.staticFileHandler) {
    test.ok(typeof http.staticFileHandler === "function",
      "http.staticFileHandler should exist when the composite feature is enabled");
    test.ok(typeof staticFileHandler === "function",
      "global staticFileHandler should exist when the composite feature is enabled");
    fileHandler = staticFileHandler(".");
    test.ok(fileHandler && typeof fileHandler.handle === "function",
      "static file handler should expose handle()");
  } else {
    test.equal(typeof http.staticFileHandler, "undefined",
      "http.staticFileHandler should stay hidden without fs support");
    test.equal(typeof staticFileHandler, "undefined",
      "global staticFileHandler should stay hidden without fs support");
  }

  return {
    port: server.port,
    staticFileHandler: features.staticFileHandler,
  };
});
