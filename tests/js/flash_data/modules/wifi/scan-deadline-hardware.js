test("wifi/scan-deadline-hardware", function () {
  var partial;
  var complete;
  var state;
  var i;
  test.equal(wifi.status().connected, false, "test requires a disconnected Station");
  wifi.start({ mode: "station", storage: "ram" });
  partial = wifi.scan({ channel: 1, mode: "passive", passiveMs: 1000,
    timeoutMs: 1, maxRecords: 4 });
  test.equal(partial.complete, false, "short deadline returns partial completion");
  test.equal(partial.timedOut, true, "deadline is observable without throwing");
  test.ok(Array.isArray(partial.records), "partial records are always an array");
  test.ok(partial.records.length <= 4, "partial results respect maxRecords");
  for (i = 0; i < 100; i += 1) {
    state = wifi.status();
    if (!state.scanDraining) break;
    Future.sleep(20).wait(1000);
  }
  test.equal(state.scanDraining, false, "native scan retires after partial completion");
  test.equal(state.scanCleanupError, null, "partial-result cleanup succeeds");
  complete = wifi.scan({ channel: 1, mode: "passive", passiveMs: 100,
    timeoutMs: 5000, maxRecords: 4 });
  test.equal(complete.complete, true, "next scan can complete normally");
  test.equal(complete.timedOut, false, "completed scan has no deadline marker");
  test.ok(Array.isArray(complete.records), "normal records use the same contract");
  return { partialRecords: partial.records.length, completeRecords: complete.records.length,
    retirementPolls: i };
});
