# Secondary LittleFS

Profiles may enable `CONFIG_ESP32QJS_SECONDARY_LITTLEFS` and configure its
partition label and base path. The runtime mounts that partition through the
same generic LittleFS API. Application bootstrap code selects the volume,
retains system and application handles, and may assign the application volume
to `globalThis.fs`. Bundled `_sys` modules remain available through
`framework.load(path)`:

```js
var systemFs = fs;
var dataFs = systemFs.volume("/data");
globalThis.fs = dataFs;
```
