# Host tooling

Top-level Python files are command entry points. Import implementation modules
from their owning package in tooling and tests; command files do not re-export
their helpers. Packages have separate responsibilities, without a shared
`esp32qjs` umbrella namespace.

| Location | Responsibility |
| --- | --- |
| `remote.py` | Development CLI: configuration, build, flash and test commands |
| `sdk_patch.py` | SDK adapter dispatch, input inspection and CMake include generation |
| `run_native_tests.py`, `run_sdk_tests.py` | Native discovery and strict SDK validation |
| `build_tools/` | Existing development CLI: configuration, build, flash, serial and test orchestration |
| `codegen/` | API/docs generation, startup precompile, font conversion and MQuickJS syntax checking |
| `capture/` | RX/CSI/Monitor decoding and PCAPNG conversion |
| `sdk_patches/` | Reviewed SDK transformations, grouped by owning component |
| `sdk_patches/common/` | GNU archive rewriting, ELF reading, exact C-source replacements and output writing |
| `tool_paths.py` | Repository root shared by the independent tools |
| `../cmake/` | Build checks and integration with ESP-IDF targets |

Existing command forms such as `python scripts/generate_api_manifest.py --check`
and `python scripts/esp32qjs_monitor.py ...` remain the public CLI. The RX and
PCAPNG libraries live exclusively in `capture`.

See [SDK patch maintenance](../docs/sdk-patches.md) for dependencies, validation
and the ESP-IDF upgrade process. Generated data belongs under the firmware-root
`build/` output directory; `scripts/build_tools/` contains source code only.
