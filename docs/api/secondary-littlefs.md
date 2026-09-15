# Filesystem mounts

Profiles may enable `CONFIG_ESP32QJS_SECONDARY_LITTLEFS` and configure its
partition label and mount path. The runtime uses the same generic LittleFS
mount API for both partitions. The primary `storage` partition is mounted at
`/framework`. A secondary partition can mount at `/`, `/data`, or another
configured path.

With a secondary partition at `/`, the default global `fs` provides one tree:

```text
/
├── index.js         application partition
├── config.json      application partition
└── framework/       separate system partition
    ├── index.js     system startup
    └── _sys/        libraries and resources
```

The ESP-IDF VFS receives an empty prefix for the root mount and selects the
longest matching prefix for nested mounts. The JavaScript namespace lists child
mount points even when their directories do not exist on the backing partition.
Mounts shadow matching backing names; they are not copied into the root image.

```js
print(fs.ROOT);                         // /
print(fs.readText("/config.json"));
print(fs.readText("/framework/_sys/fonts/map.json"));
print(JSON.stringify(fs.mounts()));
print(JSON.stringify(fs.info("/framework")));
var changes = fs.watch("/src", { capacity: 8 });
changes.close();
```

The global `fs` need not be replaced at application startup. System scripts and
`framework.load` retain their own context for relative nested `load` calls;
absolute paths always select the unified namespace. A missing secondary/root
partition does not hide the system mount or prevent recovery services starting.

Changing mount paths does not change partition names, offsets, capacities, or
stored file locations and does not require formatting. A same-named backing
file is hidden by a mount; removing the mount would expose it again. Read-only
flags remain per partition. Cross-partition rename is rejected; copying must
be explicit. File watchers capture a path filter and report absolute namespace paths.
