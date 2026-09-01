test("sys/runtime", function () {
  var info = sys.info;
  var status = sys.status;
  var timeStatus = sys.time.status();
  var features = info.features;
  var chip = info.hardware.chip;
  var runtimeInfo = info.runtime;
  var boot = status.boot;
  var internalHeap = status.memory.internal;
  var internalHeapAgain = status.memory.internal;
  var memoryManager = status.memory.manager;
  var runtimeStatus = status.runtime;
  var resources = runtimeStatus.resources;
  var watchdogStatus = runtimeStatus.watchdog;
  var startupStatus = runtimeStatus.startup;
  var rtos = status.rtos;
  var taskSnapshot = sys.tasks({ limit: 2 });
  var profile = sys.config();
  var profileAgain = sys.config();
  var profileCount = 0;
  var profileKey;
  var configKeyError = "";
  var millisBefore = sys.millis();
  var microsBefore = sys.micros();
  var heap = sys.freeHeap();
  var scopedResult;
  var randomA = sys.randomHex(16);
  var randomB = sys.randomHex(16);
  var randomLimitError = "";
  var timeoutError = "";
  var timeoutLimitError = "";
  var taskLimitError = "";
  var taskShapeError = "";
  var taskUnknownOptionError = "";
  var intrinsicOptionValidationError = "";
  var controlReasonError = "";
  var controlShapeError = "";
  var controlUnknownOptionError = "";
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
  for (profileKey in profile) {
    profileCount++;
    test.equal(profile[profileKey], sys.config(profileKey),
      "enumerated profile values should match keyed lookup");
  }
  try {
    sys.config(null);
  } catch (profileKeyError) {
    configKeyError = String(profileKeyError);
  }
  try {
    sys.tasks({ limit: 0 });
  } catch (limitError) {
    taskLimitError = String(limitError);
  }
  try {
    sys.tasks({ limt: 1 });
  } catch (optionError) {
    taskUnknownOptionError = String(optionError);
  }
  try {
    var originalObjectKeys = Object.keys;
    Object.keys = function () {
      return [];
    };
    try {
      sys.tasks({ limt: 1 });
    } catch (intrinsicOptionError) {
      intrinsicOptionValidationError = String(intrinsicOptionError);
    }
  } finally {
    Object.keys = originalObjectKeys;
  }
  try {
    sys.tasks([]);
  } catch (shapeError) {
    taskShapeError = String(shapeError);
  }
  try {
    sys.restartRuntime({ reason: "" });
  } catch (reasonError) {
    controlReasonError = String(reasonError);
  }
  try {
    sys.reboot({ delay: 1 });
  } catch (controlOptionError) {
    controlUnknownOptionError = String(controlOptionError);
  }
  try {
    sys.reboot([]);
  } catch (controlArrayError) {
    controlShapeError = String(controlArrayError);
  }

  scopedResult = sys.withTimeout(100, function () {
    return 42;
  });
  test.equal(sys.withTimeout(60000, function () { return 60; }), 60,
    "sys.withTimeout() should accept the 60 second upper bound");
  try {
    sys.withTimeout(60001, function () { return 0; });
  } catch (timeoutLimit) {
    timeoutLimitError = String(timeoutLimit);
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

  test.equal(typeof sys.info, "object", "sys.info should be a namespace object");
  test.equal(typeof sys.status, "object", "sys.status should be a namespace object");
  test.equal(typeof sys.time, "object", "sys.time should be a namespace object");
  test.equal(typeof sys.time.status, "function", "sys.time.status should exist");
  test.equal(typeof timeStatus.synchronized, "boolean",
    "time synchronization status should be boolean");
  test.equal(typeof timeStatus.synchronizing, "boolean",
    "time in-progress status should be boolean");
  test.ok(timeStatus.unixTimeMs === null || typeof timeStatus.unixTimeMs === "number",
    "wall clock should be a Unix timestamp or null before synchronization");
  test.equal(typeof sys.time.sync, features.wifi ? "function" : "undefined",
    "time synchronization should follow the networking feature");
  test.ok(typeof sys.info !== "function", "the removed sys.info() function should stay absent");
  test.equal(info.version.framework, "0.1.0", "framework version should match the release");
  test.equal(info.version.mquickjs, "2025-12-22",
    "MQuickJS version should match the vendored engine release");
  test.equal(info.version.hostApi, 1, "Host API should remain v1");
  test.ok(typeof info.version.espIdf === "string" && info.version.espIdf.length > 0,
    "ESP-IDF version should be present");
  test.equal(sys.config("APP_TEST_MISSING"), undefined,
    "sys.config(key) should return undefined for an unconfigured constant");
  test.equal(typeof profile, "object",
    "sys.config() without a key should return a profile snapshot");
  test.ok(profile !== profileAgain,
    "sys.config() should return a fresh profile snapshot");
  test.ok(profileCount <= 64,
    "sys.config() should honor the bounded hardware-profile size");
  profile.APP_TEST_MISSING = 1;
  test.equal(sys.config("APP_TEST_MISSING"), undefined,
    "mutating a profile snapshot should not change the native registry");
  test.ok(configKeyError.indexOf("string key") >= 0,
    "sys.config(key) should reject a non-string key");
  test.ok(/^hw-[0-9a-f]{12}$/.test(info.hardware.hardwareId),
    "hardware ID should derive from the factory Base MAC");
  test.ok(typeof info.hardware.target === "string" && info.hardware.target.length > 0,
    "build target should be present");
  test.ok(typeof chip.model === "string" && chip.model.length > 0, "chip model should be present");
  test.ok(chip.revision.raw >= 0 && chip.cores >= 1, "chip snapshot should contain revision and cores");
  test.ok(info.hardware.cpu.configuredFrequencyHz > 0, "configured CPU frequency should be present");
  test.ok(info.hardware.flash.sizeBytes > 0, "Flash size should be present");
  test.ok(typeof info.hardware.psram.enabled === "boolean", "PSRAM availability should be explicit");

  test.ok(/^([0-9a-f]{16})$/.test(boot.bootId), "boot ID should be a 64-bit lowercase hex value");
  test.ok(boot.uptimeMs >= 0, "boot uptime should be non-negative");
  test.ok(typeof boot.reset.code === "number" && typeof boot.reset.name === "string",
    "reset status should contain raw and normalized values");
  test.ok(boot.wakeup.names instanceof Array, "wakeup names should be an array");
  test.ok(status.cpu.frequencyHz === null || status.cpu.frequencyHz > 0,
    "live CPU frequency should be positive or unsupported");
  test.ok(internalHeap !== internalHeapAgain, "structured getters should return detached fresh objects");
  test.ok(internalHeap.totalBytes >= internalHeap.freeBytes,
    "heap totals should be internally coherent");
  test.ok(memoryManager.pressure === "normal" ||
      memoryManager.pressure === "guarded" ||
      memoryManager.pressure === "critical",
    "memory manager pressure should use the stable v1 states");
  test.ok(memoryManager.internalReserveBytes > 0 &&
      memoryManager.dmaLargestReserveBytes > 0,
    "memory manager should publish non-zero internal and DMA reserves");
  test.ok(memoryManager.managedInternalBytes >= 0 &&
      memoryManager.managedPsramBytes >= 0 &&
      memoryManager.driverPinnedBytes >= 0 &&
      memoryManager.pendingDmaReservationBytes >= 0 &&
      memoryManager.movableIdleBytes >= 0,
    "memory manager byte counters should be non-negative");
  test.ok(memoryManager.migrationCount >= 0 &&
      memoryManager.evictionCount >= 0 &&
      memoryManager.allocationFailures >= 0,
    "memory manager operation counters should be non-negative");
  test.ok(typeof heap === "number" && heap >= 0, "sys.freeHeap() should be numeric");
  test.equal(rtos.name, "FreeRTOS", "RTOS name should be stable");
  test.equal(rtos.schedulerState, "running", "scheduler should be running in a JS test");
  test.ok(rtos.tickRateHz > 0 && rtos.taskCount > 0, "RTOS counters should be present");
  test.equal(rtos.taskSnapshotSupported, true, "test firmware should enable task snapshots");
  test.ok(taskSnapshot.total >= taskSnapshot.tasks.length, "task snapshot totals should be coherent");
  test.ok(taskSnapshot.tasks.length <= 2, "task snapshot should honor its limit");
  test.ok(taskSnapshot.tasks.length === 0 || typeof taskSnapshot.tasks[0].id === "number",
    "task snapshots should expose stable numeric IDs without handles");
  test.ok(taskLimitError.indexOf("1 through") >= 0, "task limit should be range checked");
  test.ok(taskUnknownOptionError.indexOf("unknown option") >= 0,
    "unknown task options should be rejected");
  test.ok(intrinsicOptionValidationError.indexOf("unknown option 'limt'") >= 0,
    "native option validation should not depend on mutable Object.keys");
  test.ok(taskShapeError.indexOf("expects an object") >= 0,
    "task options should reject arrays");

  test.equal(runtimeStatus.generation, 1, "the first runtime generation should be one");
  test.ok(typeof sys.safeMode === "boolean", "sys.safeMode should expose the persistent boot choice");
  test.ok(typeof watchdogStatus.systemEnabled === "boolean", "system watchdog status");
  test.ok(typeof watchdogStatus.jsEnabled === "boolean", "JavaScript watchdog status");
  test.ok(watchdogStatus.lastOuterHeartbeatAgeMs >= 0, "outer heartbeat age");
  test.ok(startupStatus.failureLimit >= 1, "startup failure limit");
  test.ok(startupStatus.healthyAfterMs >= 1000, "startup healthy window");
  test.equal(runtimeStatus.restartCount, 0, "a fresh boot should have no runtime restarts");
  test.ok(runtimeStatus.uptimeMs >= 0, "runtime generation uptime should be non-negative");
  test.ok(resources.timers.capacity >= resources.timers.active,
    "timer resource counts should be bounded");
  test.ok(resources.futures.capacity >= resources.futures.pending,
    "Future resource counts should be bounded");
  test.ok(resources.asyncPollers.capacity >= resources.asyncPollers.registered,
    "async poller resource counts should be bounded");
  test.ok(resources.orphans.capacity >= resources.orphans.pending,
    "orphan reaper resource counts should be bounded");
  test.equal(runtimeInfo.control.restartRuntime, true, "managed runtime restart should be available");
  test.equal(runtimeInfo.control.reboot, true, "managed reboot should be available");
  test.ok(runtimeInfo.control.restartTimeoutMs > 0, "restart timeout should be configured");
  test.ok(controlReasonError.indexOf("1..64") >= 0,
    "empty lifecycle-control reasons should be rejected before scheduling");
  test.ok(controlUnknownOptionError.indexOf("unknown option") >= 0,
    "unknown lifecycle-control options should be rejected");
  test.ok(controlShapeError.indexOf("expects an object") >= 0,
    "lifecycle-control options should reject arrays");

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
  expectFeature("rmt", hasObject("rmt"));
  expectFeature("i2s", hasObject("i2s"));
  expectFeature("camera", hasObject("camera"));
  expectFeature("net", hasObject("net"));
  expectFeature("usbSerial", hasObject("usbSerial"));
  expectFeature("socket", hasObject("socket") &&
    typeof globalThis.socket.openTCP === "function" &&
    typeof globalThis.socket.listenTCP === "function" &&
    typeof globalThis.socket.openUDP === "function");
  expectFeature("websocket", hasObject("websocketClient"));
  expectFeature("bitmap", hasObject("bitmap"));
  expectFeature("bitmapJpeg", hasObject("bitmap") &&
    typeof globalThis.Bitmap === "function" &&
    typeof globalThis.Bitmap.prototype.decode === "function");
  expectFeature("wifi", hasObject("wifi"));
  test.equal(typeof features.tls, "boolean",
    "sys.info.features.tls should report the build capability");
  expectFeature("http", hasObject("http") && typeof globalThis.http.fetch === "function");
  expectFeature("httpServer", hasObject("http") && typeof globalThis.http.server === "function");
  expectFeature("runtimeLogs", hasObject("runtimeLogs"));
  test.equal(hasObject("http"), features.http || features.httpServer,
    "http namespace should exist when either client or server support is enabled");

  if (features.fs) {
    test.equal(runtimeInfo.filesystem.root, fs.ROOT,
      "runtime filesystem configuration should report the primary root");
  } else {
    test.equal(typeof globalThis.fs, "undefined", "fs should be hidden when disabled");
  }

  return {
    target: info.hardware.target,
    chip: chip.model,
    bootId: boot.bootId,
    generation: runtimeStatus.generation,
    freeHeap: heap,
  };
});
