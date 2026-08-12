test("sys/runtime", function () {
  var info = sys.info();
  var features = info.features;
  var millisBefore = sys.millis();
  var microsBefore = sys.micros();
  var heap = sys.freeHeap();
  var scopedResult;
  var randomA = sys.randomHex(16);
  var randomB = sys.randomHex(16);
  var randomLimitError = "";
  var timeoutError = "";
  var timeoutLimitError = "";
  var timeoutStarted;
  var timeoutElapsed;
  var waitTimeoutError = "";
  var waitTimeoutStarted;
  var waitTimeoutElapsed;
  var millisAfter;
  var microsAfter;

  function hasObject(name) {
    return typeof globalThis[name] === "object";
  }

  function expectFeature(name, available) {
    test.ok(typeof features[name] === "boolean", "info.features." + name + " should be boolean");
    test.equal(features[name], available, "feature " + name + " should match global availability");
  }

  try {
    sys.randomHex(65);
  } catch (randomError) {
    randomLimitError = String(randomError);
  }

  scopedResult = sys.withTimeout(100, function () {
    return 42;
  });
  test.equal(sys.withTimeout(60000, function () { return 60; }), 60,
    "sys.withTimeout() should accept the 60 second upper bound");
  try {
    sys.withTimeout(60001, function () { return 0; });
  } catch (limitError) {
    timeoutLimitError = String(limitError);
  }
  timeoutStarted = sys.millis();
  try {
    sys.withTimeout(10, function () {
      while (true) {
        // The scoped native deadline must interrupt this callback.
      }
    });
  } catch (error) {
    timeoutError = String(error);
  }
  timeoutElapsed = sys.millis() - timeoutStarted;

  waitTimeoutStarted = sys.millis();
  try {
    sys.withTimeout(10, function () {
      sleep(100);
    });
  } catch (waitError) {
    waitTimeoutError = String(waitError);
  }
  waitTimeoutElapsed = sys.millis() - waitTimeoutStarted;

  sleep(5);
  millisAfter = sys.millis();
  microsAfter = sys.micros();

  test.ok(info && typeof info === "object", "sys.info() should return an object");
  test.ok(features && typeof features === "object", "sys.info().features should return an object");
  test.equal(info.runtimeVersion, "0.1.0", "runtime version should match the framework release");
  test.equal(info.hostApiVersion, 1, "Host API version should match the native compatibility level");
  test.ok(typeof info.board === "string" && info.board.length > 0, "board name should be present");
  test.ok(typeof info.chip === "string" && info.chip.length > 0, "chip name should be present");
  test.ok(typeof info.freeHeap === "number" && info.freeHeap >= 0, "info.freeHeap should be numeric");
  test.ok(typeof heap === "number" && heap >= 0, "sys.freeHeap() should be numeric");
  test.equal(scopedResult, 42, "sys.withTimeout() should return the callback result");
  test.ok(timeoutLimitError.indexOf("1 and 60000") >= 0,
    "sys.withTimeout() should reject a budget above 60 seconds");
  test.equal(randomA.length, 32, "sys.randomHex() should return two characters per byte");
  test.ok(/^[0-9a-f]+$/.test(randomA), "sys.randomHex() should return lowercase hex");
  test.ok(randomA !== randomB, "sys.randomHex() should return fresh random data");
  test.ok(randomLimitError.indexOf("1..64") >= 0,
    "sys.randomHex() should reject oversized requests");
  test.ok(timeoutError.indexOf("deadline exceeded") >= 0,
    "sys.withTimeout() should report runaway JavaScript as a catchable deadline error");
  test.ok(timeoutElapsed >= 10 && timeoutElapsed < 200,
    "sys.withTimeout() should enforce its scoped deadline");
  test.ok(waitTimeoutError.indexOf("deadline exceeded") >= 0,
    "sys.withTimeout() should remain active during native waits");
  test.ok(waitTimeoutElapsed >= 10 && waitTimeoutElapsed < 100,
    "native waits should be sliced at the scoped deadline");
  test.ok(millisAfter >= millisBefore, "sys.millis() should be monotonic");
  test.ok(microsAfter >= microsBefore, "sys.micros() should be monotonic");

  expectFeature("fs", hasObject("fs"));
  expectFeature("nvs", hasObject("nvs"));
  expectFeature("gpio", hasObject("gpio"));
  expectFeature("ledc", hasObject("ledc"));
  expectFeature("adc", hasObject("adc"));
  expectFeature("dac", hasObject("dac"));
  expectFeature("i2c", hasObject("i2c"));
  expectFeature("spi", hasObject("spi"));
  expectFeature("uart", hasObject("uart"));
  expectFeature("usbSerial", hasObject("usbSerial"));
  expectFeature("socket", hasObject("socket") &&
    typeof globalThis.socket.tcp === "object" &&
    typeof globalThis.socket.udp === "object");
  expectFeature("websocket", hasObject("websocketClient"));
  expectFeature("displayBuffer", hasObject("displayBuffer"));
  expectFeature("wifi", hasObject("wifi"));
  expectFeature("http", hasObject("http") && typeof globalThis.fetch === "function");
  expectFeature("httpServer", hasObject("http") && typeof globalThis.http.server === "function");
  test.equal(hasObject("http"), features.http || features.httpServer,
    "http namespace should exist when either client or server support is enabled");

  if (features.fs) {
    test.equal(info.scriptsDir, fs.ROOT, "info should report the filesystem root");
  } else {
    test.equal(typeof globalThis.fs, "undefined", "fs should be hidden when disabled");
  }

  return {
    board: info.board,
    chip: info.chip,
    freeHeap: heap,
    features: features,
  };
});
