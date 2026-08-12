# API Reference

This project exposes a small JavaScript runtime with an optional REPL. Run `help()` on the device to point back to these documents.

The API is split into two parts:

- [docs/c-api.md](c-api.md)
  C-side host APIs exported by the firmware runtime, including global helpers, `fs`, `nvs`, `Stream`, `Request`, `Response`, `gpio`, `ledc`, `adc`, `dac`, `i2c`, `spi`, `uart`, `displayBuffer`, `sys`, `socket`, `wifi`, and `http`.
- [docs/js-api.md](js-api.md)
  JavaScript-side APIs loaded from LittleFS, currently including `display` and `ui`.
- [docs/api-stability-plan.md](api-stability-plan.md)
  Draft long-term API plan covering freeze targets, modules that still need adjustment, and current next-step candidates.
- [docs/runtime-api.md](runtime-api.md)
  Native C lifecycle and integration API for custom firmware entry points.
- [docs/backlog.md](backlog.md)
  Production, recovery, security, extensibility, and verification work that has
  not yet been implemented.

Recommended reading order:

1. `c-api.md` for the built-in runtime and transport APIs.
2. `api-stability-plan.md` for the remaining long-term API direction and active roadmap.
3. `js-api.md` for higher-level JS helpers layered on top of the built-ins.
4. `runtime-api.md` for embedding and lifecycle integration.
5. `backlog.md` for work that remains outside the current API implementation.
