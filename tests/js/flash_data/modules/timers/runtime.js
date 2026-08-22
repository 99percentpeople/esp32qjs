test("timers/runtime", function () {
  var invoked = false;
  var queued = Future.call(function (value) {
    invoked = true;
    return value + 1;
  }, null, [41]);
  var left;
  var right;
  var combined;
  var raced;
  var rejected;
  var cancelled;
  var timeoutInput;
  var timed;
  var rejectedCaught = false;
  var timeoutCaught = false;
  var fireAndForgetRan = false;
  var completedHandle;
  var futureWave;
  var futureIndex;
  var futureBatch;
  var retainedTerminalHandles = [];
  var capacityFutures = [];
  var capacityCaught = false;
  var intervalTicks = 0;
  var intervalId;
  var reentrantTicks = 0;
  var reentrantDepth = 0;
  var maximumReentrantDepth = 0;
  var reentrantIntervalId;
  var idleJobRan = false;
  var waitTimeoutFuture;
  var waitTimeoutIntervalId;
  var waitTimeoutStarted;
  var waitTimeoutElapsed;
  var waitTimeoutError = "";

  test.ok(typeof Future === "function", "Future factory should exist");
  test.ok(typeof EventQueue === "function", "EventQueue class should exist");
  test.ok(typeof defer === "undefined", "legacy defer should be removed");
  test.ok(typeof waitFor === "undefined", "legacy waitFor should be removed");
  test.equal(queued.status(), "queued", "Future.call should defer invocation");
  test.ok(!invoked, "Future.call must not invoke JavaScript before returning");
  test.equal(queued.wait(1000), 42, "queued JavaScript future result");
  test.ok(invoked, "wait should pump queued work");
  queued = null;
  gc();

  Future.call(function () {
    fireAndForgetRan = true;
  });
  delay(20);
  test.ok(fireAndForgetRan,
    "unretained Future should start and settle at a later scheduler safe point");

  completedHandle = Future.call(function () { return 7; });
  delay(20);
  test.equal(completedHandle.status(), "fulfilled",
    "completed Future handle should retain terminal status after releasing its scheduler slot");
  test.equal(completedHandle.wait(0), 7,
    "completed Future handle should retain its result after releasing its scheduler slot");

  for (futureWave = 0; futureWave < 3; futureWave++) {
    futureBatch = [];
    for (futureIndex = 0; futureIndex < 6; futureIndex++) {
      futureBatch.push(Future.sleep(10));
    }
    for (futureIndex = 0; futureIndex < futureBatch.length; futureIndex++) {
      futureBatch[futureIndex].wait(1000);
      retainedTerminalHandles.push(futureBatch[futureIndex]);
    }
  }
  test.equal(Future.call(function () { return 9; }).wait(1000), 9,
    "retained terminal Future handles should not exhaust active scheduler capacity");
  test.equal(retainedTerminalHandles[0].status(), "fulfilled",
    "retained handles should preserve terminal status after scheduler slot reuse");
  completedHandle = null;
  futureBatch = null;
  retainedTerminalHandles = null;
  gc();

  for (futureIndex = 0; futureIndex < 8; futureIndex++) {
    capacityFutures.push(Future.sleep(1000));
  }
  try {
    Future.sleep(1000);
  } catch (capacityError) {
    capacityCaught = String(capacityError).indexOf("Future capacity is exhausted") >= 0;
  }
  test.ok(capacityCaught, "public Future capacity should remain bounded");
  test.equal(typeof fs.exists("."), "boolean",
    "native synchronous adapters should use internal slots when public capacity is full");
  for (futureIndex = 0; futureIndex < capacityFutures.length; futureIndex++) {
    capacityFutures[futureIndex].cancel();
  }
  capacityFutures = null;
  gc();

  left = Future.sleep(20);
  right = Future.call(function () { return "right"; });
  combined = Future.all([left, right]);
  test.equal(combined.wait(1000)[1], "right", "Future.all should preserve input order");
  left = null;
  right = null;
  combined = null;
  gc();

  raced = Future.race([Future.sleep(10), Future.sleep(80)]);
  test.equal(raced.wait(1000).index, 0, "Future.race should report the first input");
  raced = null;
  gc();

  rejected = Future.call(function () { throw "future-fail"; });
  try {
    rejected.wait(1000);
  } catch (rejectionError) {
    rejectedCaught = String(rejectionError).indexOf("future-fail") >= 0;
  }
  test.ok(rejectedCaught, "Future rejection should surface from wait");
  rejected = null;
  gc();

  cancelled = Future.sleep(1000);
  test.ok(cancelled.cancel(), "pending Future should be cancellable");
  test.equal(cancelled.status(), "cancelled", "cancel should update status");
  cancelled = null;
  gc();

  timeoutInput = Future.sleep(500);
  timed = Future.timeout(timeoutInput, 10);
  try {
    timed.wait(1000);
  } catch (timeoutError) {
    timeoutCaught = String(timeoutError).indexOf("expired") >= 0;
  }
  test.ok(timeoutCaught, "Future.timeout should reject and cancel its input");
  timeoutInput = null;
  timed = null;
  gc();

  intervalId = setInterval(function () {
    intervalTicks++;
    if (intervalTicks >= 3) clearInterval(intervalId);
  }, 10);
  Future.sleep(80).wait(500);
  test.equal(intervalTicks, 3, "Future wait should keep timer callbacks progressing");

  reentrantIntervalId = setInterval(function () {
    reentrantTicks++;
    reentrantDepth++;
    if (reentrantDepth > maximumReentrantDepth) {
      maximumReentrantDepth = reentrantDepth;
    }
    if (reentrantTicks >= 3) {
      clearInterval(reentrantIntervalId);
    } else {
      Future.sleep(30).wait(500);
    }
    reentrantDepth--;
  }, 10);
  Future.sleep(120).wait(500);
  test.equal(reentrantTicks, 3,
    "repeating timer should continue after a callback pumps nested Future work");
  test.equal(maximumReentrantDepth, 1,
    "repeating timer callback should not re-enter itself during nested Future polling");

  waitTimeoutFuture = Future.sleep(200);
  waitTimeoutIntervalId = setInterval(function () {
    intervalTicks++;
  }, 1);
  waitTimeoutStarted = sys.millis();
  try {
    waitTimeoutFuture.wait(20);
  } catch (waitError) {
    waitTimeoutError = String(waitError);
  }
  waitTimeoutElapsed = sys.millis() - waitTimeoutStarted;
  clearInterval(waitTimeoutIntervalId);
  waitTimeoutFuture.cancel();
  test.ok(waitTimeoutError.indexOf("timed out after 20 ms") >= 0,
    "ready timers must not starve a finite Future.wait timeout");
  test.ok(waitTimeoutElapsed < 150,
    "Future.wait timeout should not be delayed until the input Future completes");

  sys._deferIdle(function () {
    idleJobRan = true;
  });
  Future.sleep(30).wait(500);
  test.ok(!idleJobRan,
    "top-level idle jobs must not run from a nested Future wait");

  test.equal(delay(5100), 5100, "long native delay should cooperate with the task watchdog");

  return { invoked: invoked, intervalTicks: intervalTicks };
});
