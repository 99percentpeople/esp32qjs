# Repository Guidelines

## Project Structure

This is the independent, reusable ESP-IDF framework. Intrinsic MCU defaults live
under `configs/mcus/`; resolved product inputs arrive only through an immutable
Build Context. The framework must not interpret Board, JavaScript Library,
Agent, workspace, or provider manifests.

The reusable lifecycle component is `components/esp32qjs_runtime/`, the default
entry point is `main/`, and feature-gated MQuickJS adapters live in
`components/esp32_mquickjs/`. The optional serial shell is
`components/esp32qjs_interactive/`. Host C tests are under `tests/c/`, device JS
tests under `tests/js/`, Python tooling tests under `tests/python/`, and complete
test Build Context fixtures under `tests/build-contexts/`. Generated output
stays under `build/`.

The user-facing and AI-facing JavaScript API reference is shared under
`docs/api/` and selected by `docs/api/docs.json`. These are ordinary
documentation, not Skills. Problem-solving Skills belong to a host product and
are outside this repository.

## Build, Flash, and Debug

Use the repository helper for normal development:

- `uv sync`
- `cp .env.example .env`, then set `IDF_PATH` and optionally `TARGET`.
- `python scripts/remote.py mcus`
- `python scripts/remote.py show-config`
- `python scripts/remote.py check-js`
- `python scripts/remote.py --assume y build`
- `python scripts/remote.py flash`
- `python scripts/remote.py flash-fs`
- `python scripts/remote.py monitor`
- `python -m unittest discover -s tests/python`
- `python scripts/remote.py test --scope c`
- `python scripts/remote.py test`

The default is the repository's ESP32-S3 test Build Context. To consume a Hub
output, pass `--build-context /absolute/path/to/context`. A context must already
contain its exact v1 manifest, sdkconfig defaults, partition table, constants,
precompile manifest, and `flash_data`; do not add fallback readers for deleted
application profiles or hardware templates.

`flash --erase-workspace` and `flash-workspace` are destructive. Ordinary
flashes preserve the workspace. Use direct `idf.py` only for low-level ESP-IDF
configuration work.

## Coding and Versioning

Use 4-space indentation and ESP-IDF C conventions. Prefer `snake_case` for
functions and locals, `UPPER_SNAKE_CASE` for macros, and thin adapter helpers
around third-party code. Keep ESP32-specific changes outside the vendored
MQuickJS submodule unless the issue is demonstrably upstream.

Project-owned APIs, manifests, and persisted schemas remain on the sole v1
contract during development. Breaking changes replace v1 directly; do not add
v2 identifiers, legacy aliases, compatibility parsers, or migration branches.

Flashed JavaScript uses the vendored ES5-like dialect. Do not use Node syntax
checking as authority or introduce `const`, `let`, classes, arrows, or template
literals without an explicit transpilation pipeline.

## Testing and Commits

The default validation target is `python scripts/remote.py test`. Use
`--scope`, `--module`, `--network`, `--loopback`, and `--media-hardware` only to
narrow or explicitly enable physical cases. Network cases require the
`TEST_WIFI_*` variables; loopback and media tests require matching hardware.

Prefer extending `tests/c`, `tests/js`, or `tests/python` over putting test logic
in `app_main()`. In pull requests, state the exact build/flash commands, board,
and hardware evidence. Keep commits narrow and imperative.
