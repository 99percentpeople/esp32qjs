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
  var limitError = null;
  var invalidLimitError = "";
  var unknownOptionError = "";
  var atomicTempFound = false;
  var boundedChanges;
  var boundedFirst;
  var boundedSecond;
  var boundedStats;
  var watchCapacityError = "";
  var watchOptionsError = "";

  test.equal(fs.ROOT, "/littlefs", "fs root");
  systemFs = fs;
  sameRootFs = systemFs.volume("/littlefs");
  test.equal(sameRootFs.ROOT, "/littlefs", "volume captures its root");
  globalThis.fs = sameRootFs;
  test.equal(systemFs.ROOT, "/littlefs", "existing volume remains immutable");
  info = fs.info();
  test.equal(info.root, fs.ROOT, "fs info root");
  test.equal(info.readOnly, false, "test filesystem remains writable");
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
  test.equal(change.sequence, 1, "watch sequence should start at one");
  test.ok(change.timestampUs > 0, "watch events should include a timestamp");
  test.equal(mirrorChanges.receive(0).path, dirPath, "independent watcher path");
  test.ok(mirrorChanges.close(), "independent watcher should close");
  test.ok(fs.stat(dirPath).isDir, "mkdir should create a directory");

  bytes = fs.writeText(filePath, "hello");
  test.equal(bytes, 5, "writeText byte count");
  change = changes.receive(0);
  test.equal(change.type, "write", "watch write type");
  test.equal(change.path, filePath, "watch write path");
  test.equal(fs.readText(filePath), "hello", "readText round-trip");
  test.equal(fs.readText(filePath, { maxBytes: 5 }), "hello",
    "readText should accept content at the configured limit");
  try {
    fs.readText(filePath, { maxBytes: 4 });
  } catch (readLimitError) {
    limitError = readLimitError;
  }
  test.ok(limitError !== null, "readText should reject content above maxBytes");
  test.equal(limitError.code, "FS_READ_LIMIT_EXCEEDED",
    "readText limit error code");
  test.equal(limitError.maxBytes, 4, "readText limit error maxBytes");
  test.equal(limitError.actualBytes, 5, "readText limit error actualBytes");
  test.ok(String(limitError.path).indexOf(filePath) >= 0,
    "readText limit error path");
  try {
    fs.readText(filePath, { maxBytes: 0 });
  } catch (invalidReadLimitError) {
    invalidLimitError = String(invalidReadLimitError);
  }
  test.ok(invalidLimitError.indexOf("1..") >= 0,
    "readText should reject maxBytes below the configured range");
  try {
    fs.readText(filePath, { limit: 5 });
  } catch (unknownReadOptionError) {
    unknownOptionError = String(unknownReadOptionError);
  }
  test.ok(unknownOptionError.indexOf("unknown key 'limit'") >= 0,
    "readText should reject unknown options");
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
  entries = fs.list(dirPath);
  for (index = 0; index < entries.length; index += 1) {
    if (entries[index].name.indexOf(".qjs-") === 0) {
      atomicTempFound = true;
    }
  }
  test.ok(!atomicTempFound,
    "successful atomic writeText should not leave temporary files");

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

  boundedChanges = fs.watch({ capacity: 2 });
  test.equal(boundedChanges.stats().capacity, 2,
    "fs.watch should apply the requested bounded capacity");
  fs.writeText("watch-overflow-a.txt", "a");
  fs.writeText("watch-overflow-b.txt", "b");
  fs.writeText("watch-overflow-c.txt", "c");
  boundedStats = boundedChanges.stats();
  test.equal(boundedStats.queued, 2,
    "bounded filesystem watcher should retain its configured event count");
  test.equal(boundedStats.dropped, 1,
    "filesystem watcher should count a dropped oldest event");
  boundedFirst = boundedChanges.receive(0);
  boundedSecond = boundedChanges.receive(0);
  test.equal(boundedFirst.sequence, 2,
    "a dropped oldest filesystem event should leave a detectable sequence gap");
  test.equal(boundedSecond.sequence, 3,
    "filesystem event sequence should advance per matching change");
  test.ok(boundedSecond.timestampUs >= boundedFirst.timestampUs,
    "filesystem event timestamps should be monotonic");
  boundedChanges.close();
  fs.remove("watch-overflow-a.txt");
  fs.remove("watch-overflow-b.txt");
  fs.remove("watch-overflow-c.txt");

  try {
    fs.watch({ capacity: 0 });
  } catch (invalidWatchCapacity) {
    watchCapacityError = String(invalidWatchCapacity);
  }
  test.ok(watchCapacityError.indexOf("1..64") >= 0,
    "fs.watch should reject a capacity outside its public bound");
  try {
    fs.watch({ cap: 2 });
  } catch (invalidWatchOptions) {
    watchOptionsError = String(invalidWatchOptions);
  }
  test.ok(watchOptionsError.indexOf("unknown key 'cap'") >= 0,
    "fs.watch should reject unknown option keys");

  for (index = 0; index < 20; index += 1) {
    leaked = fs.open("index.js", "r");
    leaked = null;
    gc();
  }
  test.ok(true, "Stream finalizers release unclosed slots");

  return { entries: entries.length };
});
