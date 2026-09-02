# Native operational errors

Argument and option validation fails before dispatch with the ordinary
`TypeError` or `RangeError` appropriate to the invalid input. Recoverable
native operational failures use one v1 shape:

```js
{
  name: "Error",
  message: "human-readable diagnostic",
  code: "STABLE_MODULE_CODE",
  operation: "module.operation",
  details: { /* bounded, module-specific metadata */ }
}
```

`code` is the stable programmatic discriminator, `operation` identifies the
failed native operation, and every module-specific numeric or status field is
nested under `details`. The sole v1 contract has no legacy top-level detail
aliases. Error details exclude payload bytes, Wi-Fi passwords, BLE/ESP-NOW
keys, certificate contents, and other secrets. The exact module unions are
declared by `NativeError`, `SPIError`, `TlsError`, `WiFiError`, `EspNowError`,
`BLEError`, and `HTTPError` in `types/esp32qjs-c-api.d.ts`.
