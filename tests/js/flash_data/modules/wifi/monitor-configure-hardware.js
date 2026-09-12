test("wifi/monitor-configure-hardware", function () {
  function settleCapture(value, closing) {
    var attempt;
    var current;
    for (attempt = 0; attempt < 100; attempt += 1) {
      try {
        if (closing) value.close();
        else value.stop();
      } catch (cleanupError) {
        current = value.status();
        if (!current.cleanupPending) throw cleanupError;
      }
      current = value.status();
      if (!current.cleanupPending && current.state === (closing ? "closed" : "stopped")) return;
      Future.sleep(10).wait(1000);
    }
    throw new Error("Monitor cleanup did not settle within the functional-test budget");
  }
  var session = null;
  var pending = null;
  var before = wifi.status().radio.clients.wifiMonitor;
  var state;
  var generation;
  var failed;
  var i;
  test.equal(wifi.monitor.capabilities().supports.configure, true, "configure is callable");
  try {
    session = wifi.monitor.open({
      filter: { types: [] },
      capture: { snapLength: 24 },
      buffering: { poolCapacity: 2, queueCapacity: 2 }
    });
    generation = session.status().generation;
    failed = false;
    try { session.configure({ filter: { types: [] } }); }
    catch (runningError) { failed = true; }
    test.ok(failed, "running configure must reject before replacing the Session");
    test.equal(session.status().generation, generation, "running rejection preserves identity");
    settleCapture(session, false);
    pending = Future.call(session.receive, session, [3000]);
    for (i = 0; i < 10; i += 1) {
      state = session.configure({
        channel: "current",
        filter: { types: [], sampleEvery: i + 1 },
        capture: { snapLength: 48 + i, requireComplete: i % 2 === 0 },
        buffering: { poolCapacity: 3 + i % 2, queueCapacity: 4 + i % 2 }
      });
      test.equal(state.state, "stopped", "configure does not restart capture");
      test.ok(state.generation > generation, "replacement gets a new monotonic pool identity");
      generation = state.generation;
      test.equal(state.snapLength, 48 + i, "snapLength changes with the pool");
      test.equal(state.poolCapacity, 3 + i % 2, "pool capacity changes");
      test.equal(state.queueCapacity, 4 + i % 2, "queue capacity changes");
      test.equal(wifi.status().radio.clients.wifiMonitor, before + 1, "Radio lease survives replacement");
      if (pending !== null) {
        test.equal(pending.wait(3000), null, "old pending receive terminates on the old closed queue");
        pending = null;
      }
      test.equal(session.receive(0), null, "direct receive selects the replacement queue");
      test.equal(session.receiveBatch({ timeoutMs: 0 }), null, "empty replacement Batch poll returns null");
      test.equal(Future.call(session.receive, session, [0]).wait(1000), null,
        "native Future selects the replacement queue");
      failed = false;
      try { session.configure({ buffering: { poolCapacity: 129 } }); }
      catch (invalidError) { failed = true; }
      test.ok(failed, "invalid replacement is rejected");
      test.equal(session.status().generation, generation, "invalid options preserve identity");
      session.start();
      settleCapture(session, false);
    }
    settleCapture(session, true);
    test.equal(session.status().poolRetained, false, "closed idle replacement returns its pool");
    test.equal(wifi.status().radio.clients.wifiMonitor, before, "close releases the one transferred lease");
    gc();
  } finally {
    if (pending !== null) pending.cancel();
    if (session !== null) settleCapture(session, true);
  }
});
