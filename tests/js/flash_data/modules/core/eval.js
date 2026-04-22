__esp32qjsTest.run("core/eval", function () {
  var value = 1 + 2 + 3;

  __esp32qjsTest.equal(value, 6, "basic eval");
  __esp32qjsTest.equal(SCRIPTS_DIR, "/littlefs", "scripts dir");
  gc();
  help();
  delay(2);

  return { value: value, scriptsDir: SCRIPTS_DIR };
});
