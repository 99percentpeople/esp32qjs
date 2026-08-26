test("fs/filesystem", function () {
  var dirPath = "tmp-test-dir";
  var filePath = dirPath + "/tmp-test.txt";
  var renamedPath = dirPath + "/tmp-test-renamed.txt";
  var bytes;
  var appended;
  var stat;
  var entries;
  var info;
  var changes;
  var mirrorChanges;
  var pendingChange;
  var change;
  var systemFs;
  var sameRootFs;
  var pendingRead;
  var laneWrite;
  var laneAppend;
  var laneRead;
  var laneResults;
  var stream;
  var pendingStreamRead;
  var streamText;
  var leaked;
  var index;

  test.equal(fs.ROOT, "/littlefs", "fs root");
  systemFs = fs;
  sameRootFs = systemFs.volume("/littlefs");
  test.equal(sameRootFs.ROOT, "/littlefs", "volume captures its root");
  globalThis.fs = sameRootFs;
  test.equal(systemFs.ROOT, "/littlefs", "existing volume remains immutable");
  info = fs.info();
  test.equal(info.root, fs.ROOT, "fs info root");
  test.ok(info.totalBytes > 0, "fs info total bytes");
  test.ok(info.usedBytes >= 0, "fs info used bytes");
  test.equal(info.freeBytes, info.totalBytes - info.usedBytes, "fs info free bytes");
  changes = fs.watch();
  mirrorChanges = fs.watch();
  pendingChange = Future.call(changes.receive, changes, [1000]);
  fs.mkdir(dirPath);
  change = pendingChange.wait(1000);
  test.equal(change.type, "mkdir", "watch mkdir type");
  test.equal(change.path, dirPath, "watch mkdir path");
  test.equal(mirrorChanges.receive(0).path, dirPath, "independent watcher path");
  test.ok(mirrorChanges.close(), "independent watcher should close");
  test.ok(fs.stat(dirPath).isDir, "mkdir should create a directory");

  bytes = fs.writeText(filePath, "hello");
  test.equal(bytes, 5, "writeText byte count");
  change = changes.receive(0);
  test.equal(change.type, "write", "watch write type");
  test.equal(change.path, filePath, "watch write path");
  test.equal(fs.readText(filePath), "hello", "readText round-trip");
  appended = fs.appendText(filePath, "!");
  test.equal(appended, 1, "appendText byte count");
  test.equal(changes.receive(0).type, "write", "watch append type");
  test.equal(fs.readText(filePath), "hello!", "appendText round-trip");
  pendingRead = Future.call(systemFs.readText, systemFs, [filePath]);
  test.equal(pendingRead.wait(1000), "hello!",
    "explicit Future captures its FsVolume receiver");

  laneWrite = Future.call(systemFs.writeText, systemFs,
    [filePath, "lane-first"]);
  laneAppend = Future.call(systemFs.appendText, systemFs,
    [filePath, "-second"]);
  laneRead = Future.call(systemFs.readText, systemFs, [filePath]);
  laneResults = Future.all([laneWrite, laneAppend, laneRead]).wait(2000);
  test.equal(laneResults[2], "lane-first-second",
    "same-volume Futures should execute in submission order");

  stream = Future.call(fs.open, fs, [filePath, "r"]).wait(1000);
  pendingStreamRead = Future.call(stream.read, stream, [16]);
  streamText = pendingStreamRead.wait(1000);
  test.equal(streamText, "lane-first-second", "Stream.read uses a Future driver");
  Future.call(stream.close, stream, []).wait(1000);

  stat = fs.stat(filePath);
  test.equal(stat.name, "tmp-test.txt", "stat name");
  test.equal(stat.size, 17, "stat size");

  fs.rename(filePath, renamedPath);
  change = changes.receive(0);
  test.equal(change.type, "rename", "watch rename type");
  test.equal(change.path, filePath, "watch rename source");
  test.equal(change.toPath, renamedPath, "watch rename target");
  test.ok(fs.exists(renamedPath), "renamed file should exist");

  entries = fs.list(dirPath);
  test.ok(entries.length >= 1, "directory listing should not be empty");

  fs.remove(renamedPath);
  test.equal(changes.receive(0).type, "remove", "watch file removal");
  test.ok(!fs.exists(renamedPath), "removed file should not exist");
  fs.remove(dirPath);
  test.equal(changes.receive(0).type, "remove", "watch directory removal");
  test.ok(!fs.exists(dirPath), "removed directory should not exist");
  test.ok(changes.close(), "watch queue should close");

  for (index = 0; index < 20; index += 1) {
    leaked = fs.open("index.js", "r");
    leaked = null;
    gc();
  }
  test.ok(true, "Stream finalizers release unclosed slots");

  return { entries: entries.length };
});
