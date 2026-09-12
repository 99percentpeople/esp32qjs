test("wifi/raw-tx-hardware", function () {
  function probeRequest(address) {
    var bytes = [];
    var mac = address.split(":");
    var i;
    for (i = 0; i < 24; i += 1) bytes.push(0);
    bytes[0] = 64;
    for (i = 0; i < 6; i += 1) {
      bytes[4 + i] = 255;
      bytes[10 + i] = parseInt(mac[i], 16);
      bytes[16 + i] = 255;
    }
    // Wildcard SSID and ordinary 2.4 GHz supported rates, no caller FCS.
    bytes.push(0, 0, 1, 8, 130, 132, 139, 150, 12, 18, 24, 36);
    return bytes;
  }
  function completed(result, length) {
    test.equal(result.driverAccepted, true, "SDK must accept the frame");
    test.equal(result.driverCompleted, true, "original native completion must arrive");
    test.equal(result.byteLength, length, "result preserves captured length");
    test.equal(result.interface, "station", "completion belongs to Station");
    test.ok(result.completedAtUs >= result.submittedAtUs, "callback time follows submission");
  }
  var session = null;
  var periodic = null;
  var primaryError = null;
  var stage = "start";
  var frame;
  var first;
  var sent;
  var admission;
  var flushed;
  var before;
  var after;
  var periodicState;
  var rejected;
  var attempt;
  test.equal(wifi.status().connected, false, "test requires a disconnected Station");
  try {
    wifi.start({ mode: "station", storage: "ram" });
    wifi.setChannel(4);
    frame = probeRequest(wifi.getMac("station"));
    stage = "one-shot";
    first = wifi.rawTx.send(frame, { timeoutMs: 3000 });
    completed(first, frame.length);
    stage = "session-open";
    session = wifi.rawTx.open({ timeoutMs: 3000, queue: { capacityPackets: 2 } });
    before = session.stats();
    rejected = false;
    try { session.enqueueBatch([frame, frame, frame]); }
    catch (admissionError) { rejected = true; }
    test.ok(rejected, "oversized batch must reject before queue admission");
    test.equal(session.stats().admitted, before.admitted, "rejected batch admits no prefix");
    stage = "batch";
    admission = session.enqueueBatch([frame, frame]);
    test.equal(admission.admittedPackets, 2, "whole bounded batch admitted");
    flushed = session.flush(5000);
    test.equal(flushed.pending, 0, "batch fence completes");
    test.equal(flushed.submitted, 2, "both batch packets reach SDK");
    test.equal(flushed.settled, 2, "both batch packets settle");
    stage = "session-send";
    sent = session.send(frame, { timeoutMs: 3000 });
    completed(sent, frame.length);
    test.ok(sent.sequence > first.sequence, "native identity advances");
    stage = "periodic";
    periodic = session.startPeriodic({ frame: frame, intervalUs: 100000,
      count: 3, timeoutMs: 3000, stopOnError: true });
    for (attempt = 0; attempt < 40; attempt += 1) {
      periodicState = periodic.status();
      if (periodicState.state !== "running") break;
      Future.sleep(50).wait(1000);
    }
    test.equal(periodicState.state, "stopped", "finite periodic job stops");
    test.equal(periodicState.scheduled, 3, "finite schedule remains bounded");
    periodic.close();
    periodicState = periodic.status();
    test.equal(periodicState.state, "closed", "periodic native resources retire");
    test.ok(periodicState.completed >= 1, "at least one periodic packet completes");
    periodic = null;
    stage = "close";
    flushed = session.flush(5000);
    test.equal(flushed.pending, 0, "all admitted periodic packets settle");
    session.close();
    after = session.status();
    test.equal(after.state, "closed", "Session close completes");
    session.close();
    session = null;
    gc();
    after = wifi.status().radio;
    test.equal(after.clients.wifiRawTx, 0, "Raw TX lease returns");
    test.equal(after.rawTx.liveSessions, 0, "native Session registry returns");
    test.equal(after.rawTx.periodicJobs, 0, "native timer registry returns");
    test.equal(after.rawTx.cleanupPending, false, "no unfinished Raw TX cleanup");
    return { oneShot: first, batch: admission, flush: flushed, periodic: periodicState };
  } catch (error) {
    primaryError = error;
    error.testStage = stage;
    throw error;
  } finally {
    try {
      if (periodic !== null) periodic.close();
      if (session !== null) session.close();
      if (!wifi.status().radio.rawTx.cleanupPending) wifi.stop({ timeoutMs: 3000 });
    } catch (cleanupError) {
      if (primaryError !== null) primaryError.cleanupError = String(cleanupError);
      else throw cleanupError;
    }
  }
});
