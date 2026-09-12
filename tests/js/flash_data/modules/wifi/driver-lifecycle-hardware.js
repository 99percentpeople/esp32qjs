test("wifi/driver-lifecycle-hardware", function () {
  function checkIdle(state) {
    test.equal(state.started, false, "stopped driver must stay stopped");
    test.equal(state.radio.clients.total, 0, "stopped driver must return every owner");
    test.equal(state.radio.activeOperations, 0, "native operations must finish");
    test.equal(state.radio.lifecycleActive, false, "lifecycle reservation must retire");
    test.equal(state.radio.restartRequired, false, "healthy lifecycle must not require reboot");
    test.equal(state.radio.faultError, null, "healthy lifecycle must not leave a fault");
  }
  function sample() {
    var memory = sys.status.memory;
    return { internal: memory.internal, psram: memory.psram,
      manager: memory.manager, radio: wifi.status().radio };
  }
  var original = null;
  var before;
  var after;
  var state;
  var generation;
  var observed;
  var cycles = [];
  var stage = "start";
  var i;
  test.equal(wifi.status().connected, false, "test requires a disconnected Station");
  try {
    wifi.start({ mode: "station", storage: "ram" });
    original = wifi.driver.getScanParameters();
    wifi.driver.setScanParameters({ activeMinMs: 20, activeMaxMs: 80,
      passiveMs: 120, homeChannelDwellMs: 40 });
    stage = "warm-stop";
    checkIdle(wifi.stop({ timeoutMs: 5000 }));
    stage = "warm-restart";
    state = wifi.driver.restart({ timeoutMs: 10000 });
    test.equal(state.started, true, "restart must start the restored Station");
    test.equal(state.connected, false, "restart must not associate Station");
    checkIdle(wifi.stop({ timeoutMs: 5000 }));
    Future.sleep(50).wait(1000);
    gc();
    before = sample();
    for (i = 0; i < 3; i += 1) {
      generation = wifi.status().radio.generation;
      stage = "restart-" + i;
      state = wifi.driver.restart({ timeoutMs: 10000 });
      test.ok(state.radio.generation > generation, "physical reconstruction advances generation");
      test.equal(state.started, true, "restored Station starts");
      test.equal(state.connected, false, "restored Station stays disconnected");
      observed = wifi.driver.getScanParameters();
      test.equal(observed.activeMinMs, 20, "restart preserves active minimum");
      test.equal(observed.activeMaxMs, 80, "restart preserves active maximum");
      test.equal(observed.passiveMs, 120, "restart preserves passive dwell");
      test.equal(observed.homeChannelDwellMs, 40, "restart preserves home dwell");
      stage = "stop-" + i;
      state = wifi.stop({ timeoutMs: 5000 });
      checkIdle(state);
      cycles.push({ generation: state.radio.generation,
        clients: state.radio.clients.total, activeOperations: state.radio.activeOperations });
    }
    Future.sleep(50).wait(1000);
    gc();
    after = sample();
    test.ok(after.internal.freeBytes + 8192 >= before.internal.freeBytes,
      "warmed internal heap should return within the functional-test allowance");
    test.ok(after.internal.largestFreeBlockBytes + 32768 >= before.internal.largestFreeBlockBytes,
      "warmed largest internal block should recover");
    return { warmupCycles: 1, cycles: cycles, before: before, after: after };
  } catch (error) {
    error.testStage = stage;
    throw error;
  } finally {
    if (original !== null && !wifi.status().radio.restartRequired &&
        wifi.status().radio.faultError === null) {
      wifi.start({ mode: "station", storage: "ram" });
      wifi.driver.setScanParameters(original);
      wifi.stop({ timeoutMs: 5000 });
    }
  }
});
