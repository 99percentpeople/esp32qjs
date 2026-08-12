test("timers/capacity", function () {
  var saturated = [];
  var capacityCaught = false;
  var futureIndex;

  gc();
  for (futureIndex = 0; futureIndex < 8; futureIndex++) {
    saturated.push(Future.sleep(1000));
  }
  try {
    Future.sleep(1000);
  } catch (capacityError) {
    capacityCaught = String(capacityError).indexOf("capacity") >= 0;
  }
  test.ok(capacityCaught, "configured eight-slot Future table should be bounded");
  for (futureIndex = 0; futureIndex < saturated.length; futureIndex++) {
    test.ok(saturated[futureIndex].cancel(), "saturated Future should cancel");
  }

  return { capacity: saturated.length };
});
