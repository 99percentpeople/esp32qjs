# Test responsibilities and execution

Classify tests by the behavior they exercise and where they run. Python launching
compiled C is a native test runner. Python sending JS to a board is a device
runner. Neither operation belongs in the Python tooling test count.

## Layout

```text
tests/
  c/
    CMakeLists.txt       host CTest targets, linked against production C
    unit/<domain>/      native CTest sources: runtime, memory, io, fs, media,
                        net, protocol, ble, espnow, wireless, wifi/{config,csi,lifecycle,monitor}
    integration/        Python drivers for native C, SDK and VM regression cases
      ble/, espnow/, wireless/, memory/, runtime/
      wifi/
        lifecycle/, driver/, config/, station/, ap/, security/
        csi/, monitor/, tx/, ftm/, mesh/, nan/, twt/
        provisioning/{dpp,smartconfig,wps}/
    fixtures/<domain>/<case>/  C fragments (.inc), matching integration domains
    fixtures/shared/    C fragments used by shared VM support
    support/            reusable ble, memory_stubs and radio_stubs boundaries
  python/
    tooling/{build,generators}/     CLI/config, build inputs, parsers/generators
    contracts/{runtime,io,net,wireless}/  static source and API constraints
    infrastructure/     test discovery, evidence and fixture/harness tooling
  support/              reusable fixture readers, compiler/VM drivers, discovery
  fixtures/             shared protocol golden vectors and cross-suite inputs
  js/
    flash_data/_test/   device test harness
    flash_data/modules/ public API cases executed by the ESP32
    flash_data/fixtures/ device-side input/library fixtures
    templates/          device snippets coordinated by a host runner
    host-fixtures/      input for host tests of the test harness itself
  build-contexts/       complete immutable test Build Contexts
```

Place new tests in the owning domain, rather than adding files directly under
`c/unit/`, `c/integration/` or `python/`. Split a large domain by feature, as Wi-Fi
does above. Keep each integration driver's C fragments under the same domain
and case name in `c/fixtures/`. Shared support is organized by responsibility;
it is not a test-case directory. Device JS already groups cases by API module.

Every Python test directory contains `__init__.py` so discovery recurses through
the hierarchy. Import cross-case fixture factories by their full package path.
Use `tests.support.paths.ROOT` for repository paths; case directory depth must
not determine the source root. Native filename patterns search all domains.

The `.inc` files contain C source fragments assembled with actual production
functions, SDK types, and other boundaries by the native driver. They are not
standalone translation units. Shared compiler, extraction and VM support lives
under `tests/support/`; generated translation units and binaries stay in
`build/` or temporary directories.

## Responsibilities and limits

| Family | Owns | Execution | Evidence limit |
| --- | --- | --- | --- |
| C / native | Production native logic and bindings, resource ownership, leases, queues, callbacks, error/timeout handling, allocation/GC injection | Host CTest; native integration drivers with the real vendored VM where needed; explicitly identified target SDK/linker/emulator cases | Injected boundaries do not prove the real SDK task scheduling, radio, or physical device. |
| Python tooling | CLI/configuration, generators, parsers, manifests, feature gates, source constraints, test selection and runner correctness | Development host | A source/manifest assertion proves the checked constraint, not firmware runtime behavior. |
| Device JS | Public arguments/errors, Futures/events, Frame/Batch/View/Source semantics, GC, close/reopen, module interactions and applicable RF behavior | Vendored MQuickJS on the selected ESP32 | Record target, artifact, workspace policy, peer/peripherals, and duration. Host syntax checks cannot replace execution. |

A defect can need a deterministic C failure/race test and a public API JS device
case. They answer different questions. Exercise production code instead of
writing a second test-owned version of the state machine.

New native behavior assertions and static SDK/RTOS boundaries belong in C
fixtures. Python drivers may assemble them, extract exact production functions,
supply dynamic declarations/options, and invoke the compiler/VM/emulator. Keep
shared execution infrastructure outside `test_*.py` modules. Some existing native
cases reuse another native case's fixture factory; their imported TestCase
classes are not registered a second time.

Python tooling tests may contain short C/JS snippets as inputs to a parser,
extractor, generator, or syntax checker under test. Prefer parsed contracts and
behavior checks over exact implementation spelling. Static source checks cannot
replace executed cleanup, race, allocation-failure, or timeout regressions.

Device JS uses the MQuickJS ES5-like dialect. Keep credentials injected by the
host out of tracked files and results. `offline.js` still executes on the device;
it avoids external network/peer prerequisites. `js/host-fixtures/` calibrates
host-side tooling and test-harness behavior; it is not ESP32 or RF evidence and
is not part of flashed JS sources.

## Commands

Run commands from `firmware/`.

```sh
# All native execution: CMake/CTest, then the C/SDK/VM integration suite.
uv run python scripts/remote.py test --scope c

# Native integrations alone, or one filename pattern; no device build/flash.
uv run python scripts/run_native_tests.py
uv run python scripts/run_native_tests.py --pattern 'test_wifi_late_station_netif.py'
uv run python scripts/run_native_tests.py --list
uv run python scripts/run_sdk_tests.py
uv run python scripts/run_native_tests.py --case tests.c.integration.wifi.lifecycle.test_wifi_start_events.WiFiStartEvents.test_cached_started_still_checks_exact_radio_owner_and_native_result

# Python tooling and static contract tests only.
uv run python -m unittest discover -s tests/python
uv run python -m unittest discover -s tests/python -p 'test_native_test_layout.py'
uv run python -m unittest discover -s tests/python/contracts/wireless

# Syntax only; no public API execution.
uv run python scripts/remote.py check-js
```

Use `run_native_tests.py` for integration discovery. Generic unittest discovery
also registers imported TestCase classes; the native loader registers only
classes defined in each selected module, while retaining inherited methods of
locally defined subclasses. Empty selections and import failures fail the run.
`--case` accepts repeatable qualified case/class/module selectors and removes
duplicate selections. `--result PATH` records selected and executed counts,
per-case outcomes, fixture failures, skip reasons, and elapsed time. Class/module
setup failures account for every affected selected case. Early-stopped cases
remain not-run and make the run unsuccessful.

`--require-all` also fails a native run if any selected case is skipped. The
dedicated SDK runner selects the tooling/native modules registered in the SDK
patch catalog and enforces this rule. Add `--camera` after resolving the locked
managed camera component. Ordinary host runs may still skip unavailable SDK
prerequisites; CI's separate SDK job supplies them and requires execution.

The standalone native runner consumes `IDF_PATH` from its environment. The CLI
C scope passes the resolved SDK from its project configuration when available;
an unavailable SDK remains an explicit fixture prerequisite skip. An SDK source
tree and the corresponding target tools/emulator are needed for SDK cases.

Device execution uses `remote.py test --scope js --module ...` and its explicit
network, wireless, loopback, media, and soak gates. It normally builds and
flashes dedicated test inputs. Establish the exact target and Build Context
before a physical run; see the framework
[test commands](../README.md#flash-monitor-and-test).

The default `remote.py test` includes C and device JS scopes. Python tooling
checks are a separate invocation. CI runs that tooling invocation and the C
scope; the hardware-lab workflow owns device execution.

## Reporting and maintenance

Report CTest, native C/SDK/VM integrations, Python tooling/contracts, generated
artifact checks, JS syntax, target builds, and device results separately. Do not
add the same native cases to the Python tooling count or infer a feature
completion percentage from the number of tests. Preserve original failures and
skip reasons. RF, coexistence, and soak evidence must state what actually ran.

The reorganization preserved all 1,131 distinct cases from the previous
Python-discovered suite: 756 native integration cases and 375 tooling/contract
cases before adding runner-specific tests. Its previous 1,253 executions
included 122 duplicate registrations of imported TestCase classes. The lower
new total removes duplicate execution, not regression coverage. These are
migration evidence counts, not permanently expected suite sizes.

The subsequent domain split preserves all 756 native integration and 386
tooling cases, including the added runner tests. CTest target names remain
stable. Directory moves change qualified Python case IDs; use the current
`--list` output for explicit selectors. Recorded results retain their original
IDs, with the move mapping kept in the investigation evidence.

For routine changes, run affected cases and necessary generation/syntax checks.
Group wider regression and target builds at a feature or stage boundary; test
layout and documentation changes do not require rebuilding/flashing all device
targets. Missing peers remain `not-run`; short functional passes are not soak
qualification.
