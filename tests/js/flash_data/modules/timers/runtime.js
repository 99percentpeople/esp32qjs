test("timers/runtime", function () {
  var deferred = defer();
  var deferredRejected = defer();
  var intervalTicks = waitFor(function (resolve) {
    var ticks = 0;
    var intervalId = setInterval(function () {
      ticks++;
      if (ticks >= 3) {
        clearInterval(intervalId);
        resolve(ticks);
      }
    }, 20);
  }, 1000);
  var timeoutValue = waitFor(function (resolve) {
    setTimeout(function () {
      resolve("timeout");
    }, 30);
  }, 1000);
  var clearedTimeoutResult;
  var clearedIntervalTicks;
  var deferredRejectedCaught = false;
  var waitForRejectedCaught = false;
  var callbackTimeoutRecovery;

  clearedTimeoutResult = waitFor(function (resolve) {
    var ran = false;
    var timeoutId = setTimeout(function () {
      ran = true;
    }, 20);

    clearTimeout(timeoutId);
    setTimeout(function () {
      resolve(ran);
    }, 50);
  }, 1000);

  clearedIntervalTicks = waitFor(function (resolve) {
    var ticks = 0;
    var intervalId = setInterval(function () {
      ticks++;
    }, 10);

    setTimeout(function () {
      clearInterval(intervalId);
      setTimeout(function () {
        resolve(ticks);
      }, 40);
    }, 35);
  }, 1000);

  setTimeout(function () {
    deferred.resolve("deferred-ok");
  }, 25);
  setTimeout(function () {
    deferredRejected.reject("deferred-fail");
  }, 25);

  test.equal(intervalTicks, 3, "interval tick count");
  test.equal(timeoutValue, "timeout", "timeout result");
  test.equal(deferred.wait(1000), "deferred-ok", "deferred wait");
  test.ok(!clearedTimeoutResult, "cleared timeout should not run");
  test.ok(clearedIntervalTicks >= 2, "cleared interval should tick before clear");

  try {
    deferredRejected.wait(1000);
  } catch (deferredError) {
    deferredRejectedCaught = String(deferredError).indexOf("deferred-fail") >= 0;
  }
  test.ok(deferredRejectedCaught, "deferred reject should surface");

  try {
    waitFor(function (resolve, reject) {
      setTimeout(function () {
        reject("waitFor-fail");
      }, 20);
    }, 1000);
  } catch (waitForError) {
    waitForRejectedCaught = String(waitForError).indexOf("waitFor-fail") >= 0;
  }
  test.ok(waitForRejectedCaught, "waitFor reject should surface");

  callbackTimeoutRecovery = waitFor(function (resolve) {
    setTimeout(function () {
      while (true) {
        // The native callback deadline must interrupt this loop.
      }
    }, 10);
    setTimeout(function () {
      resolve("recovered");
    }, 40);
  }, 1000);
  test.equal(callbackTimeoutRecovery, "recovered", "runtime should recover after a timed-out timer callback");
  test.equal(delay(5100), 5100, "long native delay should cooperate with the task watchdog");

  return { intervalTicks: intervalTicks, timeoutValue: timeoutValue };
});
