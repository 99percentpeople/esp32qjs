# `nvs` Module

`nvs` is an optional bounded string store backed by the default ESP-IDF NVS
partition. Enable it with `CONFIG_ESP32_MQUICKJS_FEATURE_NVS`.
`nvs.status()` reports the build's encryption state so product policy can make
an explicit secret-storage decision.

- `nvs.MAX_VALUE_BYTES`
  Maximum UTF-8 value size, currently `2048` bytes.
- `nvs.getString(namespace, key)`
  Return the stored string or `null` when the namespace/key does not exist.
- `nvs.setString(namespace, key, value)`
  Commit one string atomically and return its UTF-8 byte length. Values cannot
  contain NUL. Updates use `NVS_READWRITE_PURGE`, which asks ESP-IDF to purge
  the prior flash value rather than merely marking it deleted.
- `nvs.erase(namespace, key)`
  Purge one key and return whether it existed.
- `nvs.clear(namespace)`
  Purge every key in one namespace and return whether the namespace existed.
- `nvs.status()`
  Return `{ initialized, encrypted, maxValueBytes }`.

Namespaces and keys are 1–15 ASCII letters, digits, `_`, or `-`. Values are
bounded UTF-8 strings stored by exact namespace and key in the default NVS
partition. ESP-IDF build configuration owns encryption and key provisioning.

```js
if (!nvs.status().encrypted) {
  print("development NVS is plaintext");
}
nvs.setString("my_app", "mode", "quiet");
print(nvs.getString("my_app", "mode"));
nvs.erase("my_app", "mode");
```

Authorization remains application-level: trusted JavaScript with access to
`nvs` can read any valid namespace/key it knows. Production applications
storing secrets should configure ESP-IDF encrypted NVS and an explicit device
key lifecycle.
