test("wifi/ap-lifecycle-hardware", function () {
  function stationIp() {
    var interfaces = net.status().interfaces;
    var i;
    for (i = 0; i < interfaces.length; i += 1) {
      if (interfaces[i].defaultRoute && interfaces[i].ipv4) return interfaces[i].ipv4.address;
    }
    return null;
  }
  function options(name) {
    return { ssid: name, password: "stage-only-" + suffix, channel: 4,
      hidden: true, maxConnections: 1, pmf: "optional", dtimPeriod: 2, csaCount: 4 };
  }
  function inspect(result, name) {
    test.equal(result.started, true, "native AP startup completes");
    test.equal(result.ssid, name, "AP readback preserves SSID");
    test.equal(result.authMode, "wpa2", "AP retains requested authentication");
    test.equal(result.pairwiseCipher, "ccmp", "AP retains requested cipher");
    test.equal(result.pmf, "optional", "AP retains PMF capability");
    test.equal(result.maxConnections, 1, "AP retains client bound");
    test.equal(result.dtimPeriod, 2, "AP retains DTIM");
    test.equal(result.csaCount, 4, "AP retains CSA count");
    test.equal(typeof result.password, "undefined", "start result excludes credentials");
    test.equal(wifi.status().radio.clients.wifiAccessPoint, 1, "AP owns one lease");
    test.ok(wifi.apClients({ includeIp: true }).length <= 1, "client query stays bounded");
  }
  var cfg = test.requireConfig("wifiSsid", "wifiPassword");
  var suffix = String(sys.millis());
  var primaryError = null;
  var stage = "exclusive-stop";
  var cycles = [];
  var connection;
  var generation;
  var originalIp;
  var result;
  var state;
  var name;
  var i;
  test.equal(wifi.status().connected, false, "test starts disconnected");
  try {
    wifi.stop({ timeoutMs: 3000 });
    for (i = 0; i < 2; i += 1) {
      name = "qjs-ap-" + suffix + "-" + i;
      stage = "exclusive-start-" + i;
      result = wifi.startAP(options(name));
      inspect(result, name);
      test.equal(result.channel, 4, "exclusive AP uses requested channel");
      stage = "exclusive-stop-" + i;
      state = wifi.stopAP(5000);
      test.equal(state.radio.clients.total, 0, "exclusive AP close returns all owners");
      test.equal(state.radio.faultError, null, "exclusive AP close is healthy");
      wifi.stopAP(5000);
      cycles.push({ kind: "exclusive", generation: state.radio.generation });
    }
    stage = "station-connect";
    connection = wifi.connect(cfg.wifiSsid, { password: cfg.wifiPassword, timeoutMs: 15000 });
    generation = wifi.status().radio.generation;
    originalIp = stationIp();
    test.ok(connection.connected && originalIp !== null, "Station has an IP before AP startup");
    for (i = 0; i < 2; i += 1) {
      name = "qjs-shared-" + suffix + "-" + i;
      stage = "shared-start-" + i;
      result = wifi.startAP(options(name));
      inspect(result, name);
      state = wifi.status();
      test.equal(state.connected, true, "adding AP preserves Station connection");
      test.equal(state.radio.generation, generation, "adding AP does not rebuild Radio");
      test.equal(stationIp(), originalIp, "adding AP preserves Station IP");
      test.equal(result.channel, connection.channel, "shared AP follows Station channel");
      stage = "shared-stop-" + i;
      state = wifi.stopAP(5000);
      test.equal(state.connected, true, "removing AP preserves Station connection");
      test.equal(state.radio.generation, generation, "removing AP does not rebuild Radio");
      test.equal(state.radio.clients.wifiAccessPoint, 0, "only AP owner retires");
      test.equal(stationIp(), originalIp, "removing AP preserves Station IP");
      cycles.push({ kind: "shared", generation: state.radio.generation });
    }
    return { cycles: cycles, stationGeneration: generation, stationPreserved: true };
  } catch (error) {
    primaryError = error;
    error.testStage = stage;
    throw error;
  } finally {
    try {
      wifi.stopAP(5000);
      wifi.disconnect();
      wifi.stop({ timeoutMs: 5000 });
    } catch (cleanupError) {
      if (primaryError !== null) primaryError.cleanupError = String(cleanupError);
      else throw cleanupError;
    }
  }
});
