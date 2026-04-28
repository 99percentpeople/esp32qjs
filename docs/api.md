# API Reference

This project exposes a small JavaScript runtime with an optional REPL. Run `help()` on the device to point back to these documents.

The API is split into two parts:

- [docs/c-api.md](/home/zach/esp32qjs/docs/c-api.md)
  C-side host APIs exported by the firmware runtime, including global helpers, `fs`, `Stream`, `Request`, `Response`, `gpio`, `ledc`, `adc`, `dac`, `i2c`, `spi`, `displayBuffer`, `esp32`, `wifi`, and `http`.
- [docs/js-api.md](/home/zach/esp32qjs/docs/js-api.md)
  JavaScript-side APIs loaded from LittleFS, currently including `display` and `ui`.
- [docs/api-stability-plan.md](/home/zach/esp32qjs/docs/api-stability-plan.md)
  Draft long-term API plan covering freeze targets, modules that still need adjustment, and current next-step candidates.

Recommended reading order:

1. `c-api.md` for the built-in runtime and transport APIs.
2. `api-stability-plan.md` for the proposed long-term API direction and active roadmap.
3. `js-api.md` for higher-level JS helpers layered on top of the built-ins.
