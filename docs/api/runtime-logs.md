# `runtimeLogs` Module

When `sys.info.features.runtimeLogs` is enabled, `runtimeLogs` exposes the
bounded native log ring used by headless applications, including boot identity,
source-tagged chunks, resumable sequence reads, and overflow accounting.

- `runtimeLogs.read(afterSequence, limit, maxBytes)`
  Return `{ bootId, entries, dropped }`. Each entry contains
  `{ sequence, uptimeMs, source, text }`; `limit` is 1..16 and `maxBytes` is
  1..2048. `afterSequence` is exclusive, so callers can page without replaying
  the last entry. `dropped` is the cumulative ring-overflow count.
