test("wifi/raw-tx-lr-hardware", function () {
  function probe(address) {
    var frame = [];
    var mac = address.split(":");
    var i;
    for (i = 0; i < 24; i += 1) frame.push(0);
    frame[0] = 64;
    for (i = 0; i < 6; i += 1) {
      frame[4 + i] = frame[16 + i] = 255;
      frame[10 + i] = parseInt(mac[i], 16);
    }
    frame.push(0, 0, 1, 8, 130, 132, 139, 150, 12, 18, 24, 36);
    return frame;
  }
  var interfaces = ["station", "access-point"];
  var session = null;
  var primaryError = null;
  var stage = "configure";
  var results = [];
  var configured;
  var restored;
  var result;
  var restoredResult;
  var frame;
  var rejected;
  var options;
  var iface;
  var i;
  var savedProtocols = null;
  var enabledProtocols;
  function restoreProtocols() {
    if (savedProtocols !== null) {
      wifi.driver.configureTxRate(iface, { phy: "11g", rate: "6m" });
      wifi.driver.setProtocols(iface, savedProtocols);
      savedProtocols = null;
    }
  }
  test.equal(wifi.status().connected, false, "temporary rate test starts disconnected");
  try {
    wifi.stop({ timeoutMs: 5000 });
    for (i = 0; i < interfaces.length; i += 1) {
      iface = interfaces[i];
      stage = iface + "-configure";
      options = { mode: i === 0 ? "station" : "ap", start: false, storage: "ram" };
      if (i !== 0) options.accessPoint = { ssid: "qjs-rate-" + sys.millis(),
        password: "temporary-rate-test", channel: 4, hidden: true, maxConnections: 1 };
      wifi.configure(options);
      wifi.stop({ timeoutMs: 5000 });
      savedProtocols = wifi.driver.getProtocols(iface);
      enabledProtocols = savedProtocols.ghz2.slice();
      if (enabledProtocols.indexOf("lr") < 0) enabledProtocols.push("lr");
      wifi.driver.setProtocols(iface, { ghz2: enabledProtocols });
      configured = wifi.driver.configureTxRate(iface, { phy: "lr", rate: "lr-250k" });
      test.equal(configured.known, true, "permanent predecessor is known");
      frame = probe(wifi.getMac(iface));
      stage = iface + "-open";
      session = wifi.rawTx.open({ interface: iface, timeoutMs: 5000,
        rate: { phy: "lr", rate: "lr-500k" }, queue: { capacityPackets: 2 } });
      configured = wifi.driver.txRateStatus(iface);
      test.equal(configured.config.rate, "lr-500k", "temporary rate is recorded");
      test.equal(configured.temporaryLease.previous.rate, "lr-250k", "exact predecessor is retained");
      rejected = false;
      try { wifi.driver.configureTxRate(iface, { phy: "lr", rate: "lr-250k" }); }
      catch (writeError) { rejected = true; }
      test.ok(rejected, "another write cannot bypass the temporary lease");
      test.equal(wifi.driver.txRateStatus(iface).config.rate, "lr-500k", "rejected write leaves temporary rate intact");
      stage = iface + "-send";
      result = session.send(frame, { timeoutMs: 3000 });
      test.equal(result.interface, iface, "completion belongs to selected interface");
      test.equal(result.driverAccepted, true, "SDK accepts temporary-rate send");
      test.equal(result.driverCompleted, true, "native TX completion arrives");
      test.equal(result.rate, "lr-500k", "native completion confirms the borrowed rate");
      stage = iface + "-close";
      session.close();
      test.equal(session.status().state, "closed", "Session close completes");
      session = null;
      restored = wifi.driver.txRateStatus(iface);
      test.equal(restored.config.rate, "lr-250k", "close restores permanent predecessor");
      test.equal(restored.temporaryLease, null, "temporary lease retires");
      test.equal(wifi.status().started, false, "restoration finishes stopped");
      test.equal(wifi.status().radio.clients.total, 0, "rate owner and helper owners retire");
      stage = iface + "-verify-restored-send";
      wifi.start({ mode: i === 0 ? "station" : "ap", storage: "ram" });
      session = wifi.rawTx.open({ interface: iface, timeoutMs: 5000,
        queue: { capacityPackets: 2 } });
      restoredResult = session.send(frame, { timeoutMs: 3000 });
      test.equal(restoredResult.driverCompleted, true, "restored-rate TX completes");
      test.equal(restoredResult.rate, "lr-250k", "native completion confirms the restored rate");
      session.close();
      session = null;
      wifi.stop({ timeoutMs: 5000 });
      results.push({ interface: iface, result: result, restored: restoredResult,
        restoredRate: restored.config.rate });
      stage = iface + "-restore-protocols";
      restoreProtocols();
    }
    gc();
    test.equal(wifi.status().radio.rawTx.liveSessions, 0, "native Session slots return");
    return results;
  } catch (error) {
    primaryError = error;
    error.testStage = stage;
    error.partialResults = results;
    throw error;
  } finally {
    try {
      if (session !== null) session.close();
      if (!wifi.status().radio.rawTx.cleanupPending) {
        wifi.stop({ timeoutMs: 5000 });
        restoreProtocols();
      }
    } catch (cleanupError) {
      if (primaryError !== null) primaryError.cleanupError = String(cleanupError);
      else throw cleanupError;
    }
  }
});
