# Framework dead-code scan and cleanup

Date: 2026-09-15

## Scope and method

This pass reviewed first-party framework code in `components/` and `main/`,
including 230 C files, 247 headers and 62 C fragments. It also checked the 61
Python scripts for isolated definitions and checked C/fragment filenames against
source and build-script references. Vendored MQuickJS, managed components, the
ESP-IDF source tree and product code outside `firmware/` were not cleanup targets.

Identifier counts were used only to find candidates. Each deletion was checked
against production callers, internal/public headers, JS registration, token-pasting
macros, SDK patch scripts, linker entry points, tests and the enclosing workspace.
No orphan C/fragment files were found by the filename-reference check. The Python
candidate `visit_Enum` is a visitor callback and was retained.

Before cleanup, the existing C5, S3 and C3 compile databases were used to compile
all 668 first-party translation units to `/dev/null`, with unused-function,
unused-variable, unused-but-set-variable and unused-constant diagnostics enabled.
All units compiled. The only diagnostic was the unused `js_value_to_timeout_ms`
function, reported on all three targets. Non-static functions required the
separate caller audit because the compiler does not flag externally visible
functions as unused.

## Removed code

The cleanup removes 12 functions, 11 corresponding internal declarations, three
unused macros, one obsolete declaration comment and one unused include: 217 lines
across 19 source/header files. It changes no registered JavaScript API or public
C header. Previous uncommitted TX-result and documentation changes are preserved.

| Area | Removed symbols | Evidence |
| --- | --- | --- |
| Wi-Fi | `js_value_to_timeout_ms` | Static function with no caller; confirmed by all three target compilers |
| Wi-Fi | `esp32_mquickjs_wifi_reason_to_string` | Unused internal wrapper; the active local reason formatter remains |
| Enterprise Wi-Fi | `esp32_mquickjs_wifi_eap_activate` | Unused synchronous wrapper; current configuration captures owners and uses the Radio install path |
| HTTP | `esp32_mquickjs_http_clone_request`, `esp32_mquickjs_http_call_function` | No callers or registrations in production, scripts or tests |
| Bitmap | `esp32_mquickjs_bitmap_from_value`, `esp32_mquickjs_bitmap_chunk_bytes` | Unused internal wrappers/accessors; active bitmap conversion and export paths remain |
| Stream | `esp32_mquickjs_stream_open_file` | No callers; FS opens files and hands them to `esp32_mquickjs_stream_adopt_file`. The removed function was the only use of `unistd.h` in this unit |
| ByteView | `esp32_mquickjs_new_retained_byte_view` | Unused wrapper; the retained-view factory used by wireless callers remains |
| Memory | `esp32_mquickjs_memory_block_size` | Internal accessor with no callers |
| Monitor | `esp32_mquickjs_wifi_monitor_sink` | Unregistered forwarding wrapper; active monitor publish/subscription paths remain |
| NAN Radio | `esp32_mquickjs_wifi_radio_nan_status` | Unused wrapper; native poll/close/query paths and their locked snapshot helper remain |
| HTTP server | `ESP32_MQUICKJS_HTTP_SERVER_ERROR_TEXT_LEN` | Defined once and never referenced |
| ESP-NOW | `ESPNOW_BROADCAST_ADDRESS`, `ESPNOW_DEFAULT_CHANNEL` | Defined once and never referenced |

After deletion, none of these symbols remained in production code, build scripts,
tests or other workspace consumers. This document records their former names for
review; they are not compatibility aliases.

## Deliberately retained candidates

- `app_main`, exported runtime APIs and SDK/linker wrappers are entry points, even
  when ordinary C callers do not appear in this repository.
- The Wi-Fi driver enum-name functions are called through
  `driver_config_##kind##_name`.
- Raw TX periodic functions have production callers constructed with token pasting.
- TWT lane primitives have tests that construct names with token pasting. Their
  relationship to the current Radio state machine requires a separate ownership
  review; they were not removed based on literal reference counts.
- NAN, WPS and DPP fragments are inserted into SDK translation units by the patch
  scripts. A single occurrence inside framework C files does not prove they are dead.
- Profile constant macros are consumed by generated Build Context includes.
- Native test/diagnostic primitives were not deleted merely because the first
  lexical pass found no literal production caller.

This is a conservative cleanup with source and build evidence. It does not claim
a formal proof that every feature configuration is free of unreachable code.

## Validation

- Baseline target compiler scan: C5 222, S3 224 and C3 222 translation units passed.
- Rebuilt host CTest suite: 138/138 passed.
- Selected native C/SDK/VM integrations: 28/28 passed, zero skips. Coverage includes
  retained ByteView allocation/GC, Monitor resource/session cleanup, NAN Radio
  retirement/query, enterprise configuration/ownership, scan/connect lifecycle
  and ESP-NOW completion.
- HTTP namespace, memory manager and ESP-NOW contract checks: 39/39 passed.
- API manifest check: passed, 62 classes and 663 functions.
- Wi-Fi coverage map check: passed, 1267 classified entries.
- Complete target builds after cleanup: C5, S3 and C3 passed, with no compiler
  warnings. The C5 context enables both NAN Sync and USD; all three contexts enable
  HTTP, Bitmap, ESP-NOW and enterprise Wi-Fi. Builds used the repository helper
  with the existing immutable test Build Contexts, not connected board profiles.
- Final whitespace/error-marker check: `git diff --check` passed.
- Hardware flashing and RF tests were not performed for this source cleanup.

The first complete-build invocation stopped before compilation because the shell's
Python did not provide ESP-IDF's `rich_click` dependency. Its logs are retained;
subsequent builds source the installed ESP-IDF environment first. No dependency
installation or framework behavior change was needed to address that invocation.

Reconfiguration then exposed a stale generated Wi-Fi coverage checksum: the
preceding API-document translation updated NAN heading links in the reviewed map.
Running `scripts/generate_idf_wifi_api_map.py --write-runtime` changed only
`WIFI_COVERAGE_MAP_SHA256` in the generated coverage include. The map classifications
and runtime behavior are unchanged. This synchronization is additional to the
217-line dead-code removal above.

Local evidence, including pre-edit copies, the scoped diff, compiler diagnostics,
test logs and exact build commands, is under `build/dead-code-audit-20260915/`.
That directory is generated output and is not part of the source change.
