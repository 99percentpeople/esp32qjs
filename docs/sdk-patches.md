# SDK patch maintenance

## Ownership and build boundary

The sole reviewed SDK baseline is recorded in
[`catalog.json`](../scripts/sdk_patches/catalog.json). It identifies the
ESP-IDF commit, supported targets, build adapters, ordering dependencies,
purposes and associated tests. This is development schema 1; replacing the
baseline does not add another runtime format or a compatibility branch.

Python implementations live under `scripts/sdk_patches/`:

- `wifi/`: reviewed archives and Wi-Fi application adapters.
- `wpa/`, including `eap/` and `wps/`: supplicant source transformations.
- `netif/`, `phy/`, `camera/`: component-specific corrections.
- `common/`: reusable archive, ELF, source and generated-file operations.

`CMakeLists.txt` includes `cmake/sdk_patches/apply.cmake`. The registry generates
one ordered include under `build/.../esp32qjs_sdk_fixes/`; each adapter then
applies its existing target and feature conditions. Common Python modules and
registry files are configure dependencies, so editing a shared helper reruns
the consumers. Adapter-specific SDK files remain explicit dependencies.

Python input tables remain next to their transformations. CMake-only input
fingerprints live in `cmake_inputs.json`, and the generated include supplies
those values to CMake. `registry.reviewed_inputs()` combines both sources and
rejects conflicting fingerprints for the same SDK file. Derived archive-member
fingerprints stay with the adapter that consumes them.

All active transformations write build-local copies. The shared SDK, immutable
Build Context and managed component sources are input-only. Camera 2.1.7 checks
both input files and complete output fingerprints, then replaces the two source
entries on the camera component target. It no longer edits `managed_components`.

The catalog's `helpers` are standalone internal tools, not extra build stages.
In particular, the existing `raw_tx_protected` experiment is retained without
adding it to the build pipeline; management already owns its current admission
edits. Historical investigation records retain the paths used at the time.

## Inspect and validate

Run from `firmware/`, with the chosen SDK exported:

```sh
uv run python scripts/sdk_patch.py list
uv run python scripts/sdk_patch.py check --result build/sdk-checks/inputs.json
uv run python scripts/run_sdk_tests.py

# Include the locked camera component after Component Manager has resolved it.
uv run python scripts/run_sdk_tests.py --camera

# Broader host and generated-input checks.
uv run python -m unittest discover -s tests/python
uv run python scripts/remote.py test --scope c
uv run python scripts/remote.py check-js
```

`check` inspects every registered upstream input, even when `--target` narrows
the archive composition cases. It reports missing/changed inputs together,
checks the baseline commit, and exercises the actual Vendor IE → TX rate →
management → identity chain with CSI PP preparation and optional predecessor
patches. Generated archives stay in memory. This preflight does not compile
source patches, execute firmware, or establish RF behavior; registered fixtures
and target builds cover the next stages. Camera is checked by its separate
managed-component fixtures and the S3 build.

`run_sdk_tests.py` selects registered tooling and native fixtures, requires
`IDF_PATH`, and fails on missing dependencies or skipped cases. Its native
stage uses the canonical runner with `--require-all`; imported TestCase classes
are not registered twice. Target tools are discovered through the exported
PATH or `IDF_TOOLS_PATH`, rather than a developer's absolute installation path.
SDK execution cases also require `qemu-riscv32`.

CI separates host checks, strict SDK validation and target builds. The SDK job
runs against v6.1 and verifies its exact recorded commit. The S3 representative
build additionally checks pristine camera inputs and exact generated outputs.

## Updating ESP-IDF or a managed dependency

1. Keep the current SDK available and inspect the proposed SDK in a separate
   checkout. Record its commit, submodules and toolchain. Use a fresh build
   directory when changing the SDK or its Python environment.
2. Run the preflight against that checkout. A new commit or changed input is a
   review result, not a reason to replace hashes automatically.
3. For source corrections, inspect upstream behavior and the original failure
   regression. Remove the adapter when upstream supplies all required behavior;
   otherwise adapt the transformation and retain the regression.
4. For binary changes and private ABI/ROM hooks, inspect each supported target's
   archive members, instructions, symbols, relocations and dependent structures.
   An unchanged public API does not establish binary compatibility.
5. Review the complete predecessor chain. A change in one function can change
   a member fingerprint consumed by a different patch. Update only reviewed
   variants and test both optional-feature states. Retain drift/double-apply
   rejection and unaffected-section checks.
6. Update the sole baseline and input records together, then run SDK fixtures,
   broader host checks and the relevant C3/C5/S3 build profiles. Bug corrections
   and framework instrumentation have different retirement conditions: a vendor
   bug fix does not automatically replace the framework's lifecycle hooks.
7. Report target builds and physical acceptance separately. Hardware, RF,
   lifecycle and soak validation are additional evidence.

Espressif documents that private APIs may change incompatibly even in minor or
patch releases, and does not guarantee binary compatibility between releases:
[API conventions](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/api-conventions.html).
