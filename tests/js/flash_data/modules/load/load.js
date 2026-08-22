test("load/load", function () {
  globalThis.__loadFixtureHits = 0;
  globalThis.__loadFixtureValue = null;

  load("fixtures/load-helper.js");

  test.equal(globalThis.__loadFixtureHits, 1, "fixture load count");
  test.equal(globalThis.__loadFixtureValue, "fixture-ok", "fixture load value");

  load("fixtures/load-unicode.js");
  test.equal(globalThis.__loadUnicodeFixture, "中文🙂边界",
    "load should preserve UTF-8 comments and string literals");

  return { hits: globalThis.__loadFixtureHits };
});
