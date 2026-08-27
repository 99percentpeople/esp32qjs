# JavaScript Library boundary

ESP32QJS supports ordinary JavaScript libraries layered on top of the native
Host API, but this generic framework repository does not own product libraries
or Board wrappers. A build receives their already-resolved files in the
immutable Build Context's `flash_data/` tree.

## Loading

`framework.load(path)` loads an immutable system library from
`/littlefs/_sys/<path>`. `load(path)` loads application code from the active
filesystem volume. For example, a Build Context may provide a system library
and application entry:

```js
framework.load("vendor/device-support.js");
load("ui.js");
```

The framework defines the loader and native `bitmap`, `display`, bus, storage,
network, and media primitives. It does not define which JavaScript library
paths exist, which Board profile is registered, or which generated startup
entry loads them. Those facts belong to the selected Library and Board
documentation.

## Build Context contract

The external resolver must compose a collision-free `flash_data/` tree and a
precompile manifest before invoking the framework build. It also supplies all
required native features and constants. `firmware/` consumes those outputs and
does not read Library or Board manifests.

Flashed sources must use the vendored MQuickJS ES5-like dialect. Run:

```bash
python scripts/remote.py check-js
```

before building. Do not assume that a Node.js parser accepts the same syntax.

## Documentation boundary

The generic native reference is [c-api.md](c-api.md). Compact AI-readable
framework facts are ordinary Markdown documents under `docs/ai/`, indexed by
`docs/ai/docs.json`. External Libraries and Boards can carry their own ordinary
Markdown documentation, which a host may expose as immutable `doc://`
resources bound to an Artifact.

Skills are not documentation packages. A Skill should describe a
problem-solving workflow, such as diagnosing memory pressure or safely changing
a workspace. API facts, Library behavior, Board pins, and examples belong in
ordinary documentation instead.
