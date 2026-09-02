# `fs` Module

All `fs` operations are restricted to the immutable root captured by their
`FsVolume` receiver.

- `fs.ROOT`
  Read-only root of this volume. The initial global volume uses `"/littlefs"`.
- `fs.volume(root)`
  Create an immutable `FsVolume` for an exact mounted filesystem root. The
  framework validates the mount only; applications own policy about which
  volume to retain or install globally.
- `fs.info()`
  Return live LittleFS capacity for this volume as
  `{ root, readOnly, totalBytes, usedBytes, freeBytes }`.
- `fs.watch(options?)`
  Return an `EventQueue` for filesystem changes under this volume. Events are
  `{ sequence, timestampUs, type, path }`, with `toPath` on `rename`; `type` is
  `write`, `remove`, `rename`, or `mkdir`. `options.capacity` accepts `1..64`
  and defaults to `8`; the queue drops the oldest event when full, so a sequence
  gap and `stats().dropped` expose loss. Paths are relative to the root captured
  when the queue is created. Independent watchers may coexist; close each queue
  when it is no longer needed. Use
  `Future.call(changes.receive, changes, [timeoutMs])` to wait without blocking
  the JavaScript runtime.
- `fs.list(path = ".")`
  Return an array of entries for a directory.
- `fs.stat(path)`
  Return `{ name, path, isDir, size }` for a path.
- `fs.exists(path)`
  Return `true` if the path exists.
- `fs.readText(path, options?)`
  Read a trusted text file with an allocation bound. `options.maxBytes`
  defaults to and cannot exceed
  `CONFIG_ESP32_MQUICKJS_FS_READ_TEXT_MAX_BYTES` (65536 by default). Files over
  the selected bound throw an error with
  `{ code: "FS_READ_LIMIT_EXCEEDED", path, maxBytes, actualBytes }`; consume
  larger files through `Stream`. This convenience does not validate encoding,
  so applications that accept arbitrary bytes must use `"rb"` and own their
  decoding policy.
- `fs.open(path, mode?)`
  Open a file stream. Supported modes are `r`, `rb`, `w`, `wb`, `a`, `ab`, `r+`, `w+`, and `a+`.
  File open and file-stream `read`, `write`, `flush`, `seek`, and `close` use
  native Future drivers, so `Future.call(...)` does not block the JavaScript
  runtime task on VFS calls. Direct calls cooperatively wait on the same driver.
- `fs.writeText(path, text)`
  Atomically replace a text file and return the number of bytes written. The
  implementation writes and syncs a same-directory temporary file, closes it,
  then renames it over the destination. Watchers receive one `write` event
  only after the rename commits.
- `fs.appendText(path, text)`
  Append text and return the number of bytes written. Append is synced before
  returning but does not promise power-loss atomicity.
- `fs.mkdir(path)`
  Create one directory level.
- `fs.rename(fromPath, toPath)`
  Rename a file or directory.
- `fs.remove(path)`
  Remove a file or an empty directory.

Example:

```js
var changes = fs.watch();
var systemFs = fs;
var dataFs = fs.volume("/littlefs");
var nextChange = Future.call(changes.receive, changes, [5000]);
fs.writeText("notes.txt", "hello\n");
var pending = Future.call(dataFs.readText, dataFs, ["notes.txt"]);
print(JSON.stringify(nextChange.wait()));
print(JSON.stringify(fs.info()));
print(fs.readText("notes.txt"));
print(fs.stat("notes.txt"));
print(JSON.stringify(fs.list(".")));
fs.rename("notes.txt", "notes-old.txt");
fs.remove("notes-old.txt");
changes.close();
pending.wait();
```
