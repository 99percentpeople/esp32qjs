__esp32qjsTest.run("fs/filesystem", function () {
  var dirPath = "tmp-test-dir";
  var filePath = dirPath + "/tmp-test.txt";
  var renamedPath = dirPath + "/tmp-test-renamed.txt";
  var bytes;
  var appended;
  var stat;
  var entries;

  __esp32qjsTest.equal(fs.ROOT, "/littlefs", "fs root");
  fs.mkdir(dirPath);
  __esp32qjsTest.ok(fs.stat(dirPath).isDir, "mkdir should create a directory");

  bytes = fs.writeText(filePath, "hello");
  __esp32qjsTest.equal(bytes, 5, "writeText byte count");
  __esp32qjsTest.equal(fs.readText(filePath), "hello", "readText round-trip");
  appended = fs.appendText(filePath, "!");
  __esp32qjsTest.equal(appended, 1, "appendText byte count");
  __esp32qjsTest.equal(fs.readText(filePath), "hello!", "appendText round-trip");

  stat = fs.stat(filePath);
  __esp32qjsTest.equal(stat.name, "tmp-test.txt", "stat name");
  __esp32qjsTest.equal(stat.size, 6, "stat size");

  fs.rename(filePath, renamedPath);
  __esp32qjsTest.ok(fs.exists(renamedPath), "renamed file should exist");

  entries = fs.list(dirPath);
  __esp32qjsTest.ok(entries.length >= 1, "directory listing should not be empty");

  fs.remove(renamedPath);
  __esp32qjsTest.ok(!fs.exists(renamedPath), "removed file should not exist");
  fs.remove(dirPath);
  __esp32qjsTest.ok(!fs.exists(dirPath), "removed directory should not exist");

  return { entries: entries.length };
});
