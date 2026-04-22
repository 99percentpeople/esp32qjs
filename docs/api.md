# API Reference

This project exposes a small JavaScript runtime with an optional REPL. Run `help()` on the device to point back to these documents.

The API is split into two parts:

- [docs/c-api.md](/home/zach/esp32qjs/docs/c-api.md)
  C-side host APIs exported by the firmware runtime, including global helpers, `fs`, `Stream`, `Request`, `Response`, `i2c`, `gpio`, `esp32`, `wifi`, and `http`.
- [docs/js-api.md](/home/zach/esp32qjs/docs/js-api.md)
  JavaScript-side APIs loaded from LittleFS, currently including `display` and `ui`.

Recommended reading order:

1. `c-api.md` for the built-in runtime and transport APIs.
2. `js-api.md` for higher-level JS helpers layered on top of the built-ins.
