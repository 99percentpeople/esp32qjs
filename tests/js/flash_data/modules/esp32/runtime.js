test("esp32/runtime", function () {
  var info = esp32.info();
  var features = info.features;
  var millisBefore = esp32.millis();
  var microsBefore = esp32.micros();
  var heap = esp32.freeHeap();
  var millisAfter;
  var microsAfter;

  function hasObject(name) {
    return typeof globalThis[name] === "object";
  }

  function expectFeature(name, available) {
    test.ok(typeof features[name] === "boolean", "info.features." + name + " should be boolean");
    test.equal(features[name], available, "feature " + name + " should match global availability");
  }

  sleep(5);
  millisAfter = esp32.millis();
  microsAfter = esp32.micros();

  test.ok(info && typeof info === "object", "esp32.info() should return an object");
  test.ok(features && typeof features === "object", "esp32.info().features should return an object");
  test.equal(info.runtimeVersion, "0.1.0", "runtime version should match the framework release");
  test.equal(info.hostApiVersion, 1, "Host API version should match the native compatibility level");
  test.ok(typeof info.board === "string" && info.board.length > 0, "board name should be present");
  test.ok(typeof info.chip === "string" && info.chip.length > 0, "chip name should be present");
  test.ok(typeof info.freeHeap === "number" && info.freeHeap >= 0, "info.freeHeap should be numeric");
  test.ok(typeof heap === "number" && heap >= 0, "esp32.freeHeap() should be numeric");
  test.ok(millisAfter >= millisBefore, "esp32.millis() should be monotonic");
  test.ok(microsAfter >= microsBefore, "esp32.micros() should be monotonic");

  expectFeature("fs", hasObject("fs"));
  expectFeature("gpio", hasObject("gpio"));
  expectFeature("ledc", hasObject("ledc"));
  expectFeature("adc", hasObject("adc"));
  expectFeature("dac", hasObject("dac"));
  expectFeature("i2c", hasObject("i2c"));
  expectFeature("spi", hasObject("spi"));
  expectFeature("uart", hasObject("uart"));
  expectFeature("displayBuffer", hasObject("displayBuffer"));
  expectFeature("wifi", hasObject("wifi"));
  expectFeature("http", hasObject("http") && typeof globalThis.fetch === "function");
  expectFeature("httpServer", hasObject("http") && typeof globalThis.http.server === "function");
  expectFeature("staticFileHandler", typeof globalThis.staticFileHandler === "function");
  test.equal(hasObject("http"), features.http || features.httpServer,
    "http namespace should exist when either client or server support is enabled");

  if (features.fs) {
    test.equal(info.scriptsDir, SCRIPTS_DIR, "info should report the scripts dir");
  } else {
    test.equal(typeof globalThis.SCRIPTS_DIR, "undefined", "SCRIPTS_DIR should be hidden when fs is disabled");
  }

  return {
    board: info.board,
    chip: info.chip,
    freeHeap: heap,
    features: features,
  };
});
