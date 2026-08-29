test("espnow/offline", function () {
  var capabilities = espNow.capabilities();
  var invalidOpen;
  var invalidOpenError = "";
  var session;
  var status;
  var peer;
  var lookup;
  var peerStatus;
  var staleError = "";
  var originalObjectKeys;

  test.equal(espNow.BROADCAST_ADDRESS, "ff:ff:ff:ff:ff:ff",
    "ESP-NOW should expose its normalized broadcast address");
  test.equal(espNow.MAX_PAYLOAD_V1, 250,
    "ESP-NOW should expose the v1 payload boundary");
  test.equal(capabilities.stationInterface, true,
    "ESP-NOW v1 should use the station interface");
  test.equal(capabilities.softApInterface, false,
    "ESP-NOW v1 should reject the soft-AP interface");
  test.ok(capabilities.maxPayloadBytes >= 250,
    "ESP-NOW should support v1 payloads");

  invalidOpen = Future.call(espNow.open, espNow, [{ unknown: true }]);
  test.equal(invalidOpen.status(), "rejected",
    "native open capture should reject unknown options synchronously");
  try {
    invalidOpen.wait(100);
  } catch (openError) {
    invalidOpenError = String(openError && openError.message
      ? openError.message : openError);
  }
  test.ok(invalidOpenError.indexOf("unknown option") >= 0,
    "open should report unknown option names");
  originalObjectKeys = Object.keys;
  Object.keys = function () { return []; };
  try {
    invalidOpenError = "";
    invalidOpen = Future.call(espNow.open, espNow, [{ unknown: true }]);
    try {
      invalidOpen.wait(100);
    } catch (intrinsicOpenError) {
      invalidOpenError = String(intrinsicOpenError && intrinsicOpenError.message
        ? intrinsicOpenError.message : intrinsicOpenError);
    }
    test.ok(invalidOpenError.indexOf("unknown option") >= 0,
      "ESP-NOW options should ignore mutable Object.keys");
  } finally {
    Object.keys = originalObjectKeys;
  }

  session = espNow.open({ receiveCapacity: 2, sendTimeoutMs: 250 });
  try {
    status = session.status();
    test.equal(status.open, true, "a new ESP-NOW session should be open");
    test.equal(status.interface, "station", "session interface should be station");
    test.equal(status.maxPayloadBytes, 250,
      "the default session payload should stay v1 compatible");
    test.equal(status.peerCount, 0, "a new session should have no application peers");
    test.equal(status.pendingSends, 0, "a new session should have no pending sends");
    test.equal(status.txRecovering, false, "a new session should not be recovering");
    test.equal(session.receive(0), null,
      "an empty receive queue should return null without blocking");
    test.equal(session.peers().length, 0,
      "a new session should return an empty peer snapshot");
    test.equal(session.peer("02:00:00:00:00:01"), null,
      "peer lookup should return null for an unknown address");

    peer = session.addPeer({
      address: "02:00:00:00:00:01",
      channel: "current"
    });
    peerStatus = peer.status();
    test.equal(peerStatus.address, "02:00:00:00:00:01",
      "peer addresses should be normalized");
    test.equal(peerStatus.encrypted, false,
      "unencrypted peers should be explicit in status");
    lookup = session.peer("02:00:00:00:00:01");
    test.ok(lookup && lookup.status().open,
      "peer lookup should return a generation-checked handle");
    test.equal(session.peers().length, 1,
      "peer snapshots should include the application peer");
    test.equal(peer.update({ channel: "current" }).channel, "current",
      "peer update should use the native Future driver");
    test.equal(peer.close(), true, "peer close should remove the native peer");
    try {
      peer.status();
    } catch (peerError) {
      staleError = String(peerError && peerError.message
        ? peerError.message : peerError);
    }
    test.ok(staleError.indexOf("ESPNOW_STALE_PEER") >= 0,
      "closed peer handles should be stale");
    test.equal(session.setPowerSave({
      wakeWindowMs: 20,
      wakeIntervalMs: 100
    }), true, "power-save changes should use a native Future driver");
    test.equal(session.status().powerSave.enabled, true,
      "power-save update should be visible in status");
    test.equal(session.setPowerSave({ enabled: false }), true,
      "power-save should return to the native always-awake defaults");
    test.equal(session.status().powerSave.enabled, false,
      "disabled power-save should be visible in status");
  } finally {
    if (session && session.status) {
      session.close();
    }
  }

  return {
    maxPeers: capabilities.maxPeers,
    maxPayloadBytes: capabilities.maxPayloadBytes
  };
});
