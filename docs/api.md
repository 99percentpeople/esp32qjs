# API Reference

This project exposes a small JavaScript runtime with an optional REPL. Run `help()` on the device to point back to these documents.

The API is split into two parts:

- [docs/c-api.md](c-api.md)
  C-side host APIs exported by the firmware runtime, including global helpers,
  `fs`, `nvs`, `Stream`, `Request`, `Response`, `gpio`, `ledc`, `adc`, `dac`,
  `i2c`, `spi`, `uart`, `rmt`, `i2s`, `camera`, `bitmap`, `sys`, `socket`,
  `rpc`, `wifi`, and `http`.
- [docs/js-api.md](js-api.md)
  JavaScript-side APIs loaded from LittleFS, currently including `display` and `ui`.
- [docs/api-stability-plan.md](api-stability-plan.md)
  Current v1 freeze policy, implementation status, and the evidence still
  required before individual areas can be declared stable.
- [docs/runtime-api.md](runtime-api.md)
  Native C lifecycle and integration API for custom firmware entry points.
- [docs/native-lifecycle-contracts.md](native-lifecycle-contracts.md)
  Shared callback, finalizer, lease, EventQueue, and reaper safety invariants.
- [docs/backlog.md](backlog.md)
  Remaining production, recovery, security, extensibility, and verification
  work; completed API work is intentionally not repeated there.

Recommended reading order:

1. `c-api.md` for the built-in runtime and transport APIs.
2. `api-stability-plan.md` for current freeze policy and remaining validation.
3. `js-api.md` for higher-level JS helpers layered on top of the built-ins.
4. `runtime-api.md` for embedding and lifecycle integration.
5. `native-lifecycle-contracts.md` for callback, finalizer, lease, and reaper
   safety invariants.
6. `backlog.md` for work that remains outside the current API implementation.
