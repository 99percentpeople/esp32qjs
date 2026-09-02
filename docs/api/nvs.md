# `nvs` Module

`nvs` is an optional bounded string store backed by the default ESP-IDF NVS
partition. Enable it with `CONFIG_ESP32_MQUICKJS_FEATURE_NVS`. This module does
not enable NVS encryption or provision encryption keys; `nvs.status()` makes
the build's encryption state explicit so product policy can reject unsafe
secret storage.

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

Namespaces and keys are 1–15 ASCII letters, digits, `_`, or `-`. The binding
cannot access arbitrary partitions and does not provide iteration, numeric
values, blobs, raw security keys, partition erase, or encryption-key
provisioning.

```js
if (!nvs.status().encrypted) {
  print("development NVS is plaintext");
}
nvs.setString("my_app", "mode", "quiet");
print(nvs.getString("my_app", "mode"));
nvs.erase("my_app", "mode");
```

This is a persistence and wear-leveling boundary, not an authorization
boundary. All application JavaScript is trusted; code with access to `nvs` can
read any valid namespace/key it knows. Production applications storing secrets
must configure ESP-IDF encrypted NVS and their device key lifecycle explicitly.
