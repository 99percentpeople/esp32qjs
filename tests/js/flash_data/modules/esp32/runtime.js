test("esp32/runtime", function () {
  var info = esp32.info();
  var millisBefore = esp32.millis();
  var microsBefore = esp32.micros();
  var heap = esp32.freeHeap();
  var millisAfter;
  var microsAfter;

  sleep(5);
  millisAfter = esp32.millis();
  microsAfter = esp32.micros();

  test.ok(info && typeof info === "object", "esp32.info() should return an object");
  test.ok(typeof info.board === "string" && info.board.length > 0, "board name should be present");
  test.ok(typeof info.chip === "string" && info.chip.length > 0, "chip name should be present");
  test.equal(info.scriptsDir, SCRIPTS_DIR, "info should report the scripts dir");
  test.ok(typeof info.freeHeap === "number" && info.freeHeap >= 0, "info.freeHeap should be numeric");
  test.ok(typeof heap === "number" && heap >= 0, "esp32.freeHeap() should be numeric");
  test.ok(millisAfter >= millisBefore, "esp32.millis() should be monotonic");
  test.ok(microsAfter >= microsBefore, "esp32.micros() should be monotonic");

  return {
    board: info.board,
    chip: info.chip,
    freeHeap: heap,
  };
});
