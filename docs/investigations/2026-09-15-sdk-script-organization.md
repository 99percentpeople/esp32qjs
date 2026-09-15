# scripts organization and SDK patch review

## Scope and baseline

This change reorganizes firmware host tools and verifies the existing SDK
adapters against ESP-IDF v6.1, commit
`fff9895c82d744c7237be8847347bdd1b07c6643`, for C3, C5 and S3. It does not upgrade
ESP-IDF or establish hardware/RF acceptance.

The existing dirty management-frame patch and its native fixtures were retained.
The separate `raw_tx_protected` experiment remains available as an internal
helper, outside the build pipeline. Unrelated parent-workspace changes were
preserved.

## Final organization

- `scripts/build_tools/` contains the original development CLI modules, preserving
  their existing configuration/build/flash/serial/test responsibilities.
- `scripts/codegen/`, `scripts/capture/` and `scripts/sdk_patches/` are independent
  packages. They do not expand the development CLI package's scope.
- Top-level command files remain thin entry points; `remote.py` no longer
  re-exports every implementation helper through wildcard imports.
- `cmake/` contains build integration. Its SDK includes are ordered from one
  registry, with shared implementation files declared as configure dependencies.
- `scripts/build_tools/` avoids generic `build/` ignore patterns. The firmware
  ignore rule also explicitly targets root build output, exposing the existing
  `tests/python/tooling/build/` sources to Git.
- Parent host changes include `cmake/` in native build fingerprints and update
  the Docker MQuickJS-checker input paths. These are required consumers of the
  reorganized firmware files.

## Confirmed defects and repairs

1. The current management-frame patch changed object fingerprints consumed by
   Raw TX identity. The new regression failed for all three targets with both
   off-channel states before repair. Identity now accepts only the reviewed
   outputs of the current predecessor; drift and repeated application still fail.
2. Full archive composition exposed a second predecessor problem: the FTM repair
   and TX-rate repair touch different functions in the same API object. TX-rate
   and identity now recognize the reviewed FTM combinations. Instruction edits
   and relocation guards remain unchanged.
3. Camera preparation previously edited managed component sources. It now checks
   pristine 2.1.7 inputs, generates build-local files and replaces exactly two
   target sources. Generated outputs match the previous reviewed transformations
   byte for byte. The known previously patched local camera inputs were backed
   up under `build/tooling/sdk-patch-refactor/previous-camera/` and restored from
   the fingerprint-verified component cache.
4. A pre-existing Wi-Fi configuration fixture omitted the production timeout
   macro. Its fixture now includes the actual production macro block; all four
   affected cases passed after repair. No production timeout behavior changed.

Common archive, ELF and exact source-transformation helpers have their own
package. The registry adds a read-only check of upstream input fingerprints and
complete binary patch composition. SDK validation fails if a prerequisite is
missing or a selected fixture is skipped.

## Validation and evidence

The final validation results and logs are stored under
`build/tooling/sdk-patch-refactor/`, including `summary.json` and `evidence/`.

| Validation | Result |
| --- | --- |
| SDK preflight | 124 upstream inputs matched; 6 archive compositions passed |
| Python tooling/contracts | 395 passed, no skips |
| Strict SDK suite, including camera | 11 tooling and 131 native cases passed, no skips |
| JavaScript syntax | 71 sources and 77 documentation snippets passed |
| Generated metadata | API manifest, feature docs and 1267-entry Wi-Fi map current |
| Parent host | 2 build-fingerprint tests and backend typecheck passed |
| Docker checker inputs | Isolated declared-input compilation passed |
| Target builds | All 5 profiles below passed |

Target builds use isolated `build/sdk-patch-refactor/<target>-<profile>/`
directories and the normal `scripts/remote.py ... build` entry point:

- ESP32-C3: `representative`.
- ESP32-C5: `wireless-inventory`, `wireless-ftm`.
- ESP32-S3: `representative`, `minimal`.

All five were built using the final `scripts/build_tools/` layout. The S3
representative compile database also confirms that both camera driver sources
come from the build-local patch directory. Camera fixture inputs came from the
fingerprint-verified locked 2.1.7 component cache; actual S3 configure additionally
verified the resolved managed component.

The broad C run completed 138 CTest cases before its native stage was interrupted
to apply the requested directory changes. It is not a completed full-native
result. SDK fixtures and affected tooling consumers were run separately.
The capture consumer selection passed 27 cases and skipped one because `tshark`
was unavailable; its strict-run result correctly records that missing external
PCAPNG check as an unmet gate.

CI configuration was updated but not executed remotely. The Docker checker was
compiled from an isolated copy of its declared inputs; a complete Docker image
build was not performed. No device was flashed or otherwise exercised.

## Future SDK updates

Run the documented preflight against the proposed checkout before changing any
fingerprint. Review upstream behavior, private ABI and each complete predecessor
chain; adapt or remove corrections based on the original regression. A new
public SDK version alone does not justify replacing hashes or deleting patches.
The maintenance procedure and commands are in [SDK patch maintenance](../sdk-patches.md).
