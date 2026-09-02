# System Management Implementation Design

This is a non-normative firmware-maintainer note. The sole user-facing and
AI-facing JavaScript contract is [`docs/api/sys.md`](api/sys.md); TypeScript
shapes live in `types/esp32qjs-c-api.d.ts`. This document records only native
ownership and implementation constraints that are not useful to API consumers.

## Runtime ownership

The managed runtime task is a generation supervisor. Its outer lifetime owns:

- copied runtime configuration, task and synchronization state;
- the dedicated JavaScript heap allocation;
- primary and secondary filesystem mounts;
- boot identity and retained software-reboot reason;
- runtime logs and generation counters.

Each JavaScript generation owns its `JSContext`, engine fields, built-in and
application globals, timers, Future and EventQueue state, async pollers, startup
bytecode, callbacks, servers, sockets, buses, and peripheral handles.

Generation teardown is idempotent. A native driver that still owns state causes
another bounded drain iteration; it never causes partial context reuse or
forced task deletion. Runtime logs and their sequence remain outside generation
destruction so host deduplication stays valid after a JavaScript-only restart.

## Lifecycle safe point

`sys.restartRuntime()` and `sys.reboot()` record a pending request and return.
They never destroy the active `JSContext` or restart the MCU from a
native-to-JavaScript call frame. The supervisor acts only after the requested
delay expires and the outermost JavaScript turn unwinds.

A runtime restart proceeds through quiescing, native-owner cancellation and
drain, module deinitialization, context replacement, global installation, and
startup. If drain or recreation exceeds the configured timeout, the native
runtime follows its fixed `reboot` or `stop` failure policy; JavaScript cannot
override that policy per call.

The low-level `esp32_mquickjs` engine remains usable without the managed
runtime. It gathers chip, heap, reset, clock, and inexpensive FreeRTOS facts
directly. Managed state arrives through host snapshot and control hooks. With
no hooks installed, managed configuration is reported as unavailable and
lifecycle control fails explicitly.

`esp32qjs_runtime_config_t.install_globals` runs once per generation, so its
opaque state must outlive every restart. `esp32qjs_runtime_context()` is also
generation-scoped; native integrations must refresh cached references when the
generation changes.

## Memory-manager boundary

The public heap leaves are overlapping ESP-IDF capability views. The memory
manager separately accounts for framework-owned fixed payloads, stable blocks,
registered driver payloads, and staging pools. It does not estimate opaque
third-party metadata from heap deltas.

The `DMA_EXTERNAL` class means "prefer external DMA". On a target with
PSRAM DMA support, allocation and reallocation try PSRAM and validate the
result with the target external-DMA predicate. ESP-IDF does not require PSRAM
to appear in a synthetic `SPIRAM | DMA` heap intersection for this path.

If external allocation fails, the manager makes one internal-DMA attempt only
when the complete request fits its internal reserve policy. It does not
alternate between regions or try progressively smaller driver layouts, and it
records one allocation failure only after all permitted attempts fail. Code
that requires internal DMA uses `DMA_INTERNAL` explicitly.

`pinnedBytes` includes internal fixed and stable non-movable allocations,
`DMA_EXTERNAL` allocations that fall back to internal RAM, registered driver
DMA payloads, and committed staging pools. Movable blocks are reported
separately even while borrowed. Allocation ownership is released by the same
native lifetime that releases the payload; generation teardown catches any
remaining generation-owned blocks after JavaScript finalizers.

Relocation and eviction run only at an outer runtime safe point. Recursive
scheduler polls, driver tasks, active borrows, and DMA reservation paths never
relocate blocks. On targets without PSRAM, classification and reserve checks
remain active but migration is disabled.

## Watchdog and startup ownership

The runtime-task watchdog and outer-JavaScript watchdog are independent. VM
interrupt checks and bounded native waits may feed the system task watchdog;
only outer scheduler progress or a framework-owned cooperative wait feeds the
outer-JavaScript watchdog. Tight JavaScript therefore cannot hide a turn that
never returns.

The startup guard owns only its private `qjs_rt` NVS state. It records repeated
startup or required-secondary-filesystem failures and exposes the safe-mode
latch. The generic runtime does not decide which application files to skip;
that policy belongs to the embedding product.

## Control-plane boundary

Hardware and RTOS inspection can use ordinary bounded JavaScript execution.
Lifecycle mutation needs a dedicated authenticated operator-control path,
because the device may disconnect before a normal tool result is delivered.
Hosts establish completion by observing the expected boot ID and generation;
they never replay a lost lifecycle request automatically.

## Validation invariants

Host and device tests must cover lazy getter isolation, detached snapshots,
task snapshot bounds, native-owner drain, repeated restart without monotonic
heap or task growth, retained filesystem and log state, boot/generation
identity, safe-mode recovery, and both restart failure policies. A build or
successful flash alone is not lifecycle evidence.
