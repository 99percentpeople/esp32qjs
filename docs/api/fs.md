# `fs` Module

The default global `fs` uses the unified namespace rooted at `/`. Filesystems
are mounted into this tree: the system partition is at `/framework`, and the
runtime can mount an optional writable secondary partition at `/`. Mount
selection uses the longest matching path prefix, so `/framework/file` reaches
the system partition while `/file` reaches the root partition.

Absolute and relative `fs` paths start at `/`. Paths are normalized before I/O,
and traversal above `/` is rejected. Pending Futures and open Streams retain
their resolved paths and filesystem resources even if global `fs` changes.

`list`, `stat`, Stream paths and errors use absolute namespace paths.
`list("/")` includes mounted child directories, which shadow same-named entries
on the root partition without creating or moving files. Mount directories remain
visible even when the root partition is unavailable, for recovery. Mutations of
read-only mounts fail; renames between partitions fail without copying data.
Mount roots and directories containing mounts cannot be removed or renamed.

- `fs.ROOT`
  Read-only namespace root, always `/`.
- `fs.mounts()`
  Return active mounts as `{ root, partition, type, readOnly }[]`. `root` is the
  absolute mount point, `partition` is the Flash partition label, and `type`
  is `"littlefs"`. This is a detached snapshot; modifying it does not change
  mounts. The function does not mount or unmount filesystems.
- `fs.info(path = "/")`
  Return `{ root, partition, type, readOnly, totalBytes, usedBytes, freeBytes }`
  for the longest matching mount. `root` is that mount's path. The input can be
  a file or directory path and need not already exist. Capacity belongs to the
  selected partition, excluding nested mounts. An unavailable mount throws.
- `fs.watch(path = "/", options?)`
  Return an `EventQueue` for changes to the normalized path and its descendants.
  The path may be a file, directory, or a path that will be created later.
  Use `fs.watch(undefined, { capacity: 8 })` to omit only the path.
  Events are `{ sequence, timestampUs, type, path }`, with `toPath` on `rename`;
  `type` is `write`, `remove`, `rename`, or `mkdir`. Event paths are absolute
  namespace paths. A rename is reported if either endpoint matches, so moves
  into and out of a watched directory remain visible. Watching follows the
  selected path, not the identity of a file that moves elsewhere.
  `options.capacity` accepts `1..64` and defaults to `8`; the queue drops the
  oldest event when full, so a sequence gap and `stats().dropped` expose loss.
  Independent watchers may coexist; close each queue when no longer needed.
  Use `Future.call(changes.receive, changes, [timeoutMs])` to wait without
  blocking the JavaScript runtime.
- `fs.list(path = ".")`
  Return an array of entries for a directory.
- `fs.stat(path)`
  Return `{ name, path, isDir, size, mount, readOnly }` for a path. `mount`
  identifies an exact mount root; `readOnly` describes its owning filesystem.
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
var nextChange = Future.call(changes.receive, changes, [5000]);
fs.writeText("notes.txt", "hello\n");
var pending = Future.call(fs.readText, fs, ["notes.txt"]);
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
