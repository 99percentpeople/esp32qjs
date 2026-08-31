"""Host and hardware-backed device test discovery, staging, and execution."""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import time
from dataclasses import dataclass, field, replace
from pathlib import Path

from .build import run_streaming
from .flash import flash, flash_fs
from .profiles import BUILD_ROOT, ROOT_DIR, ProjectConfig
from .server import (
    MarkerTimeoutError,
    MonitorSession,
    close_monitor_session,
    find_last_output_line_with_prefix,
    format_output_tail,
    normalize_serial_output,
    read_monitor_chunk,
    read_monitor_until_line_prefix,
    read_monitor_until_text,
    start_monitor_session,
    try_read_monitor_until_line_prefix,
)


HOST_TEST_BUILD_DIR = BUILD_ROOT / "host-tests"


JS_TEST_DIR = ROOT_DIR / "tests" / "js"


JS_TEST_FLASH_DATA_DIR = JS_TEST_DIR / "flash_data"


JS_TEST_SDKCONFIG_DEFAULTS = JS_TEST_DIR / "sdkconfig.defaults"


JS_SYNTAX_CHECK_SCRIPT = ROOT_DIR / "scripts" / "check_js_syntax.py"


JS_TEST_READY_MARKER = "__ESP32QJS_TEST_READY__"


JS_TEST_FEATURES_PREFIX = "__ESP32QJS_TEST_FEATURES__:"


JS_TEST_PASS_PREFIX = "__TEST_PASS__:"


JS_TEST_SKIP_PREFIX = "__TEST_SKIP__:"


JS_TEST_FAIL_PREFIX = "__TEST_FAIL__:"


JS_TEST_FORBIDDEN_OUTPUT_MARKER = "__ESP32QJS_HANDLED_FUTURE_REJECTION__"


JS_TEST_CASE_START_PREFIX = "__ESP32QJS_TEST_CASE_START__:"


JS_TEST_CASE_START_RETRY_SECONDS = 2.0


JS_TEST_DETAILS_MAX_BYTES = 4096


JS_REPL_BANNER_MARKER = "Run help() for usage."


MONITOR_READY_MARKER = "--- Quit:"


CTEST_SUMMARY_RE = re.compile(r"(?m)^(\d+)% tests passed, (\d+) tests failed out of (\d+)$")


TEST_SCOPE_ORDER = ("c", "js")


@dataclass(frozen=True)
class JsTestCase:
    path: str
    required_capabilities: tuple[str, ...] = ()
    timeout_seconds: float = 15.0
    reset_before: bool = False
    reset_after: bool = False
    record_details: bool = False


@dataclass(frozen=True)
class JsTestModule:
    name: str
    cases: tuple[JsTestCase, ...]
    required_features: tuple[str, ...] = ()


JS_TEST_MODULES = (
    JsTestModule("core", (JsTestCase("modules/core/eval.js"),)),
    JsTestModule("sys", (JsTestCase("modules/sys/runtime.js"),)),
    JsTestModule("gpio", (JsTestCase("modules/gpio/basic.js"),), required_features=("gpio",)),
    JsTestModule("ledc", (JsTestCase("modules/ledc/basic.js"),), required_features=("ledc",)),
    JsTestModule("adc", (JsTestCase("modules/adc/basic.js"),), required_features=("adc",)),
    JsTestModule("dac", (JsTestCase("modules/dac/basic.js"),), required_features=("dac",)),
    JsTestModule("i2c", (JsTestCase("modules/i2c/status.js"),), required_features=("i2c",)),
    JsTestModule(
        "spi",
        (
            JsTestCase("modules/spi/basic.js"),
            JsTestCase("modules/spi/loopback.js", required_capabilities=("loopback",)),
        ),
        required_features=("spi",),
    ),
    JsTestModule(
        "uart",
        (
            JsTestCase("modules/uart/basic.js"),
            JsTestCase("modules/uart/loopback.js", required_capabilities=("loopback",)),
        ),
        required_features=("uart",),
    ),
    JsTestModule(
        "rmt",
        (JsTestCase("modules/rmt/offline.js"),),
        required_features=("rmt",),
    ),
    JsTestModule(
        "i2s",
        (JsTestCase("modules/i2s/offline.js"),),
        required_features=("i2s",),
    ),
    JsTestModule(
        "camera",
        (JsTestCase("modules/camera/offline.js"),),
        required_features=("camera",),
    ),
    JsTestModule(
        "camera-bitmap",
        (
            JsTestCase(
                "modules/camera/bitmap-hardware.js",
                required_capabilities=("media-hardware",),
                timeout_seconds=90.0,
                reset_before=True,
                reset_after=True,
            ),
        ),
        required_features=("camera", "bitmap"),
    ),
    JsTestModule("timers", (JsTestCase("modules/timers/runtime.js"),)),
    JsTestModule("fs", (JsTestCase("modules/fs/filesystem.js"),), required_features=("fs",)),
    JsTestModule("nvs", (JsTestCase("modules/nvs/basic.js"),), required_features=("nvs",)),
    JsTestModule("stream", (JsTestCase("modules/stream/stream.js"),)),
    JsTestModule("load", (JsTestCase("modules/load/load.js"),), required_features=("fs",)),
    JsTestModule(
        "bitmap",
        (
            JsTestCase("modules/bitmap/basic.js", timeout_seconds=60.0),
            JsTestCase("modules/bitmap/font.js"),
        ),
        required_features=("bitmap",),
    ),
    JsTestModule(
        "display",
        (
            JsTestCase(
                "modules/display/lifecycle.js",
                timeout_seconds=90.0,
                reset_before=True,
            ),
        ),
        required_features=("bitmap",),
    ),
    JsTestModule(
        "net",
        (JsTestCase("modules/net/offline.js"),),
        required_features=("net",),
    ),
    JsTestModule(
        "wifi",
        (
            JsTestCase("modules/wifi/offline.js"),
            JsTestCase("modules/wifi/network.js", required_capabilities=("network",), timeout_seconds=25.0),
        ),
        required_features=("wifi",),
    ),
    JsTestModule(
        "espnow",
        (
            JsTestCase("modules/espnow/offline.js"),
            JsTestCase(
                "modules/espnow/wifi-radio-hardware.js",
                required_capabilities=("wireless-hardware",),
                timeout_seconds=20.0,
                reset_before=True,
                reset_after=True,
            ),
        ),
        required_features=("espNow",),
    ),
    JsTestModule(
        "wifi_csi",
        (
            JsTestCase("modules/wifi_csi/offline.js"),
            JsTestCase(
                "modules/wifi_csi/associated-hardware.js",
                required_capabilities=("csi-hardware",),
                timeout_seconds=45.0,
                reset_before=True,
                reset_after=True,
                record_details=True,
            ),
            JsTestCase(
                "modules/wifi_csi/promiscuous-hardware.js",
                required_capabilities=("csi-hardware",),
                timeout_seconds=30.0,
                reset_before=True,
                reset_after=True,
                record_details=True,
            ),
            JsTestCase(
                "modules/wifi_csi/batch-transport-hardware.js",
                required_capabilities=("csi-hardware",),
                timeout_seconds=45.0,
                reset_before=True,
                reset_after=True,
                record_details=True,
            ),
            JsTestCase(
                "modules/wifi_csi/saturation-hardware.js",
                required_capabilities=("csi-hardware",),
                timeout_seconds=150.0,
                reset_before=True,
                reset_after=True,
                record_details=True,
            ),
            JsTestCase(
                "modules/wifi_csi/throughput-hardware.js",
                required_capabilities=("csi-hardware",),
                timeout_seconds=60.0,
                reset_before=True,
                reset_after=True,
                record_details=True,
            ),
            JsTestCase(
                "modules/wifi_csi/soak-hardware.js",
                required_capabilities=("csi-hardware", "csi-soak"),
                timeout_seconds=3720.0,
                reset_before=True,
                reset_after=True,
                record_details=True,
            ),
            JsTestCase(
                "modules/wifi_csi/lifecycle-hardware.js",
                required_capabilities=("csi-hardware",),
                timeout_seconds=180.0,
                reset_before=True,
                reset_after=True,
                record_details=True,
            ),
        ),
        required_features=("wifiCsi", "wifi", "fs", "rpc"),
    ),
    JsTestModule(
        "wifi_csi_camera",
        (
            JsTestCase(
                "modules/wifi_csi/camera-coexistence-hardware.js",
                required_capabilities=("csi-hardware", "media-hardware"),
                timeout_seconds=150.0,
                reset_before=True,
                reset_after=True,
                record_details=True,
            ),
        ),
        required_features=("wifiCsi", "wifi", "camera", "bitmap"),
    ),
    JsTestModule(
        "wifi_csi_ble",
        (
            JsTestCase(
                "modules/wifi_csi/ble-coexistence-hardware.js",
                required_capabilities=("csi-hardware",),
                timeout_seconds=90.0,
                reset_before=True,
                reset_after=True,
                record_details=True,
            ),
        ),
        required_features=("wifiCsi", "wifi", "ble"),
    ),
    JsTestModule(
        "wifi_csi_tls",
        (
            JsTestCase(
                "modules/wifi_csi/tls-coexistence-hardware.js",
                required_capabilities=("csi-hardware", "network"),
                timeout_seconds=150.0,
                reset_before=True,
                reset_after=True,
                record_details=True,
            ),
        ),
        required_features=("wifiCsi", "wifi", "socket", "tls"),
    ),
    JsTestModule(
        "wifi_csi_espnow",
        (
            JsTestCase(
                "modules/wifi_csi/espnow-conflict-hardware.js",
                required_capabilities=("csi-hardware",),
                timeout_seconds=60.0,
                reset_before=True,
                reset_after=True,
                record_details=True,
            ),
        ),
        required_features=("wifiCsi", "wifi", "espNow"),
    ),
    JsTestModule(
        "ble",
        (JsTestCase("modules/ble/offline.js", timeout_seconds=45.0),),
        required_features=("ble",),
    ),
    JsTestModule(
        "http",
        (
            JsTestCase("modules/http/offline.js"),
            JsTestCase("modules/http/network.js", required_capabilities=("network",), timeout_seconds=40.0),
        ),
        required_features=("http",),
    ),
    JsTestModule(
        "http_server",
        (JsTestCase("modules/http_server/offline.js"),),
        required_features=("httpServer",),
    ),
    JsTestModule(
        "socket",
        (JsTestCase("modules/socket/offline.js"),),
        required_features=("socket",),
    ),
    JsTestModule(
        "rpc",
        (JsTestCase("modules/rpc/offline.js"),),
        required_features=("rpc",),
    ),
    JsTestModule(
        "websocket",
        (
            JsTestCase("modules/websocket/offline.js"),
            JsTestCase("modules/websocket/network.js", required_capabilities=("network",), timeout_seconds=30.0),
        ),
        required_features=("websocket",),
    ),
    JsTestModule(
        "future",
        (
            JsTestCase("modules/timers/capacity.js", reset_before=True, reset_after=True),
            JsTestCase("modules/timers/automatic-gc.js", reset_before=True, reset_after=True),
        ),
    ),
)


JS_TEST_MODULE_MAP = {module.name: module for module in JS_TEST_MODULES}


JS_TEST_CAPABILITY_FLAGS = {
    "network": "--network",
    "loopback": "--loopback",
    "media-hardware": "--media-hardware",
    "wireless-hardware": "--wireless-hardware",
    "csi-hardware": "--csi-hardware",
    "csi-soak": "--csi-soak",
}


@dataclass
class TestStageSummary:
    name: str
    status: str = "passed"
    passed_cases: int = 0
    skipped_cases: int = 0
    failed_cases: int = 0
    selection_label: str = ""
    selection: tuple[str, ...] = ()
    note: str = ""
    failure_details: list[str] = field(default_factory=list)

    @property
    def total_cases(self) -> int:
        return self.passed_cases + self.skipped_cases + self.failed_cases


@dataclass
class TestRunReport:
    scopes: tuple[str, ...]
    stages: list[TestStageSummary]

    @property
    def passed_cases(self) -> int:
        return sum(stage.passed_cases for stage in self.stages)

    @property
    def skipped_cases(self) -> int:
        return sum(stage.skipped_cases for stage in self.stages)

    @property
    def failed_cases(self) -> int:
        return sum(stage.failed_cases for stage in self.stages)

    @property
    def total_cases(self) -> int:
        return sum(stage.total_cases for stage in self.stages)

    @property
    def status(self) -> str:
        return "failed" if any(stage.status != "passed" for stage in self.stages) else "passed"


class TestStageError(RuntimeError):
    """Raised when a test stage fails after producing a summary."""

    def __init__(self, summary: TestStageSummary, message: str):
        super().__init__(message)
        self.summary = summary
        self.message = message


@dataclass(frozen=True)
class JsCaseResult:
    case: JsTestCase
    case_name: str
    status: str
    error: str = ""
    details: object | None = None


def parse_test_js_config(raw_config: str) -> dict[str, object]:
    """Decode optional JSON test configuration injected into the JS harness."""
    if not raw_config:
        return {}
    try:
        payload = json.loads(raw_config)
    except json.JSONDecodeError as exc:
        raise SystemExit(f"TEST_JS_CONFIG must be a JSON object ({exc}).") from exc
    if not isinstance(payload, dict):
        raise SystemExit("TEST_JS_CONFIG must be a JSON object.")
    return payload


def resolve_test_scopes(raw_scopes: list[str] | None) -> tuple[str, ...]:
    """Resolve selected test stages, defaulting to both C and JS in execution order."""
    if not raw_scopes:
        return TEST_SCOPE_ORDER

    selected = set(raw_scopes)
    return tuple(scope for scope in TEST_SCOPE_ORDER if scope in selected)


def resolve_js_modules(raw_modules: list[str] | None) -> tuple[JsTestModule, ...]:
    """Resolve JS test modules while preserving the registry order."""
    if not raw_modules:
        return JS_TEST_MODULES

    selected = set(raw_modules)
    return tuple(module for module in JS_TEST_MODULES if module.name in selected)


def resolve_js_test_capabilities(args: argparse.Namespace) -> set[str]:
    """Resolve opt-in JS test capabilities from CLI flags."""
    capabilities: set[str] = set()
    if args.network:
        capabilities.add("network")
    if args.loopback:
        capabilities.add("loopback")
    if args.media_hardware:
        capabilities.add("media-hardware")
    if args.wireless_hardware:
        capabilities.add("wireless-hardware")
    if args.csi_hardware:
        capabilities.add("csi-hardware")
    if args.csi_soak:
        capabilities.add("csi-soak")
    return capabilities


def describe_case_capabilities(capabilities: tuple[str, ...]) -> str:
    """Render required JS test capabilities as user-facing CLI flags."""
    return ", ".join(JS_TEST_CAPABILITY_FLAGS.get(capability, capability) for capability in capabilities)


def format_js_case_details(details: object) -> str:
    """Render one deterministic bounded evidence object for hardware-lab logs."""
    encoded = json.dumps(
        details,
        ensure_ascii=True,
        separators=(",", ":"),
        sort_keys=True,
    )
    encoded_bytes = encoded.encode("utf-8")
    if len(encoded_bytes) <= JS_TEST_DETAILS_MAX_BYTES:
        return encoded
    return json.dumps(
        {"bytes": len(encoded_bytes), "truncated": True},
        separators=(",", ":"),
        sort_keys=True,
    )


def parse_ctest_summary(output: str) -> tuple[int, int] | None:
    """Extract failed and total test counts from ctest output when available."""
    match = CTEST_SUMMARY_RE.search(output)
    if match is None:
        return None
    failed = int(match.group(2))
    total = int(match.group(3))
    return failed, total


def print_test_stage_summary(summary: TestStageSummary) -> None:
    """Print one stage summary in a format shared by C and JS tests."""
    extras: list[str] = []
    if summary.selection:
        label = summary.selection_label or "selection"
        extras.append(f"{label}={','.join(summary.selection)}")
    if summary.note:
        extras.append(f"note={summary.note}")

    suffix = f" {' '.join(extras)}" if extras else ""
    print(
        f"{summary.name} summary: "
        f"status={summary.status} total={summary.total_cases} "
        f"passed={summary.passed_cases} skipped={summary.skipped_cases} failed={summary.failed_cases}"
        f"{suffix}",
        flush=True,
    )


def print_test_report(report: TestRunReport) -> None:
    """Print the final report for the whole `test` command run."""
    scopes = ",".join(report.scopes) if report.scopes else "<none>"
    print("Test report:", flush=True)
    for summary in report.stages:
        extras: list[str] = []
        if summary.selection:
            label = summary.selection_label or "selection"
            extras.append(f"{label}={','.join(summary.selection)}")
        if summary.note:
            extras.append(f"note={summary.note}")
        suffix = f" {' '.join(extras)}" if extras else ""
        print(
            f"- {summary.name}: status={summary.status} total={summary.total_cases} "
            f"passed={summary.passed_cases} skipped={summary.skipped_cases} failed={summary.failed_cases}"
            f"{suffix}",
            flush=True,
        )
        if summary.failure_details:
            print(f"  {summary.name} failures:", flush=True)
            for detail in summary.failure_details:
                print(f"  - {detail}", flush=True)
    print(
        f"Overall: status={report.status} scopes={scopes} stages={len(report.stages)} "
        f"total={report.total_cases} passed={report.passed_cases} "
        f"skipped={report.skipped_cases} failed={report.failed_cases}",
        flush=True,
    )


def run_host_c_tests() -> TestStageSummary:
    """Configure, build, and run the host-native C test suite."""
    summary = TestStageSummary(name="C")
    print(f"Running host C tests in {HOST_TEST_BUILD_DIR}", flush=True)
    try:
        subprocess.run(
            ["cmake", "-S", str(ROOT_DIR / "tests" / "c"), "-B", str(HOST_TEST_BUILD_DIR)],
            cwd=ROOT_DIR,
            check=True,
        )
        subprocess.run(
            ["cmake", "--build", str(HOST_TEST_BUILD_DIR)],
            cwd=ROOT_DIR,
            check=True,
        )
    except subprocess.CalledProcessError as exc:
        summary.status = "failed"
        summary.note = f"build step exited with code {exc.returncode}"
        print_test_stage_summary(summary)
        raise TestStageError(summary, "Host C tests failed during configure/build.") from exc

    ctest_result = subprocess.run(
        ["ctest", "--test-dir", str(HOST_TEST_BUILD_DIR), "--output-on-failure"],
        cwd=ROOT_DIR,
        check=False,
        capture_output=True,
        text=True,
    )
    if ctest_result.stdout:
        print(ctest_result.stdout, end="")
    if ctest_result.stderr:
        print(ctest_result.stderr, end="", file=sys.stderr)

    counts = parse_ctest_summary(f"{ctest_result.stdout}\n{ctest_result.stderr}")
    if counts is not None:
        failed, total = counts
        summary.passed_cases = total - failed
        summary.failed_cases = failed
    else:
        summary.note = "ctest summary unavailable"

    if ctest_result.returncode != 0:
        summary.status = "failed"
        if not summary.note:
            summary.note = f"ctest exited with code {ctest_result.returncode}"
        print_test_stage_summary(summary)
        raise TestStageError(summary, "Host C tests failed.") from subprocess.CalledProcessError(
            ctest_result.returncode,
            ctest_result.args,
            output=ctest_result.stdout,
            stderr=ctest_result.stderr,
        )

    print_test_stage_summary(summary)
    return summary


def wait_for_optional_js_repl_banner(session: MonitorSession, initial_output: str) -> None:
    """Wait for the REPL banner when observable, but let the runtime probe prove readiness."""
    if JS_REPL_BANNER_MARKER in initial_output:
        return

    try:
        read_monitor_until_text(session, JS_REPL_BANNER_MARKER, 25.0, "the JS REPL banner")
    except MarkerTimeoutError:
        print("JS REPL banner was not observed; probing the runtime directly", flush=True)


def send_js_command(session: MonitorSession, command: str) -> None:
    """Send one JavaScript command line to the remote REPL."""
    payload = f"{command}\r".encode("utf-8")
    os.write(session.master_fd, payload)


def stage_selected_js_test_flash_data(
    build_dir: Path,
    modules: tuple[JsTestModule, ...],
) -> Path:
    """Stage only explicitly selected JS modules plus the common test harness."""
    stage_dir = build_dir / "esp32qjs-selected-test-data"
    if stage_dir.exists():
        shutil.rmtree(stage_dir)
    stage_dir.mkdir(parents=True)

    for source in JS_TEST_FLASH_DATA_DIR.iterdir():
        if source.name == "modules":
            continue
        destination = stage_dir / source.name
        if source.is_dir():
            shutil.copytree(source, destination)
        else:
            shutil.copy2(source, destination)

    for module in modules:
        for case in module.cases:
            source = JS_TEST_FLASH_DATA_DIR / case.path
            if not source.is_file():
                raise SystemExit(f"JS test case data does not exist: {source}")
            destination = stage_dir / case.path
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, destination)

    return stage_dir


def js_test_build_config(
    config: ProjectConfig,
    modules: tuple[JsTestModule, ...] = (),
    explicit_module_selection: bool = False,
) -> ProjectConfig:
    """Resolve a dedicated test Build Context with test instrumentation and LittleFS."""
    build_dir = BUILD_ROOT / config.mcu / f"{config.build_context_id}-js-test"
    flash_data_dir = (
        stage_selected_js_test_flash_data(build_dir, modules)
        if explicit_module_selection
        else JS_TEST_FLASH_DATA_DIR
    )
    target_defaults = JS_TEST_DIR / f"sdkconfig.{config.idf_target}.defaults"
    test_defaults = [JS_TEST_SDKCONFIG_DEFAULTS]
    if target_defaults.is_file():
        test_defaults.append(target_defaults)
    target_psram_defaults = (
        JS_TEST_DIR / f"sdkconfig.{config.idf_target}.psram.defaults"
    )
    if config.psram_mode != "none" and target_psram_defaults.is_file():
        test_defaults.append(target_psram_defaults)
    target_nopsram_defaults = (
        JS_TEST_DIR / f"sdkconfig.{config.idf_target}.nopsram.defaults"
    )
    if config.psram_mode == "none" and target_nopsram_defaults.is_file():
        test_defaults.append(target_nopsram_defaults)
    context_dir = build_dir / "esp32qjs-test-context"
    if context_dir.exists():
        shutil.rmtree(context_dir)
    context_dir.mkdir(parents=True)
    shutil.copytree(flash_data_dir, context_dir / "flash_data")
    for name in ("partitions.csv", "profile-constants.inc"):
        shutil.copy2(config.build_context_dir / name, context_dir / name)
    manifest = json.loads(
        (config.build_context_manifest).read_text(encoding="utf-8")
    )
    manifest["contextId"] = (
        f"{manifest.get('contextId', config.build_context_id)}-js-test"
    )
    (context_dir / "manifest.json").write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="utf-8"
    )
    (context_dir / "sdkconfig.defaults").write_text(
        "\n".join(
            path.read_text(encoding="utf-8").rstrip()
            for path in (config.build_context_sdkconfig_defaults, *test_defaults)
            if path is not None
        ) + "\n",
        encoding="utf-8",
    )
    (context_dir / "precompile.json").write_text(
        json.dumps({
            "version": 1,
            "entry": "index.js",
            "output": "index.js",
            "inline": ["_test/harness.js"],
            "removeAfterCompile": ["_test"],
        }, indent=2) + "\n",
        encoding="utf-8",
    )
    cmake_entries = [
        entry for entry in config.cmake_cache_entries
        if not entry.startswith("-DESP32QJS_BUILD_CONTEXT_DIR=")
    ]
    cmake_entries.append(f"-DESP32QJS_BUILD_CONTEXT_DIR={context_dir}")

    return replace(
        config,
        build_dir=build_dir,
        generated_sdkconfig=build_dir / config.generated_sdkconfig.name,
        build_context_manifest=context_dir / "manifest.json",
        build_context_dir=context_dir,
        build_context_flash_data_dir=context_dir / "flash_data",
        build_context_sdkconfig_defaults=context_dir / "sdkconfig.defaults",
        sdkconfig_defaults=tuple(
            path for path in (config.mcu_sdkconfig_defaults, context_dir / "sdkconfig.defaults")
            if path is not None
        ),
        partition_table=context_dir / "partitions.csv",
        profile_constants_file=context_dir / "profile-constants.inc",
        flash_data_override=flash_data_dir,
        cmake_cache_entries=tuple(cmake_entries),
    )


def ensure_js_test_runtime(session: MonitorSession) -> None:
    """Confirm that the test bootstrap is active, either after boot or via a live probe."""
    ready_output = try_read_monitor_until_line_prefix(session, JS_TEST_READY_MARKER, 25.0)
    if ready_output is not None:
        return

    send_js_command(
        session,
        'print("__ESP32QJS_TEST_PROBE__:" + (typeof globalThis.test === "function" ? "ready" : "missing"))',
    )
    probe_output = read_monitor_until_line_prefix(session, "__ESP32QJS_TEST_PROBE__:", 5.0, "the JS test runtime probe")
    probe_line = find_last_output_line_with_prefix(probe_output, "__ESP32QJS_TEST_PROBE__:")
    if probe_line != "__ESP32QJS_TEST_PROBE__:ready":
        raise SystemExit(
            "The current LittleFS image does not expose the JS test runtime. "
            "Rerun without `--no-flash-fs` to flash the test storage image."
        )


def probe_js_runtime_features(session: MonitorSession) -> dict[str, bool]:
    """Read the runtime feature map from the lazy `sys.info.features` namespace."""
    send_js_command(
        session,
        'print("__ESP32QJS_TEST_FEATURES__:" + JSON.stringify(sys.info.features))',
    )
    output = read_monitor_until_line_prefix(
        session,
        JS_TEST_FEATURES_PREFIX,
        25.0,
        "the JS runtime feature probe",
    )
    feature_line = find_last_output_line_with_prefix(output, JS_TEST_FEATURES_PREFIX)
    if feature_line is None:
        raise SystemExit("The JS runtime feature probe did not return a structured result.")
    try:
        payload = json.loads(feature_line[len(JS_TEST_FEATURES_PREFIX):])
    except json.JSONDecodeError as exc:
        raise SystemExit(f"The JS runtime feature probe emitted invalid JSON ({exc}).") from exc

    features: dict[str, bool] = {}
    for key, value in payload.items():
        if isinstance(value, bool):
            features[str(key)] = value
    return features


def configure_js_test_runtime(
    session: MonitorSession,
    config: ProjectConfig,
) -> None:
    """Push dynamic test configuration such as Wi-Fi credentials to the REPL."""
    test_config: dict[str, object] = {
        "wifiSsid": config.test_wifi_ssid,
        "wifiPassword": config.test_wifi_password,
        "httpUrl": config.test_http_url,
    }
    test_config.update(parse_test_js_config(config.test_js_config))
    encoded = json.dumps(json.dumps(test_config, separators=(",", ":")))

    send_js_command(
        session,
        (
            f"globalThis.testConfig = JSON.parse({encoded}); "
            'print("__ESP32QJS_TEST_CONFIG__")'
        ),
    )
    read_monitor_until_line_prefix(session, "__ESP32QJS_TEST_CONFIG__", 5.0, "the JS test config handshake")


def collect_js_runtime(session: MonitorSession) -> None:
    """Run a best-effort JS GC cycle between test cases to reduce cross-case state pressure."""
    try:
        send_js_command(
            session,
            'if (typeof gc === "function") gc(); print("__ESP32QJS_TEST_GC__")',
        )
        read_monitor_until_line_prefix(session, "__ESP32QJS_TEST_GC__", 5.0, "the JS GC handshake")
    except (MarkerTimeoutError, OSError):
        print("Warning: skipped JS GC handshake before the next case", flush=True)


def reset_js_test_runtime(session: MonitorSession, config: ProjectConfig) -> None:
    """Hard-reset the mcu and restore dynamic test configuration for an isolated case."""
    os.write(session.master_fd, b"\x14\x12")
    read_monitor_until_line_prefix(
        session,
        JS_TEST_READY_MARKER,
        25.0,
        "the isolated JS test runtime reset",
    )
    configure_js_test_runtime(session, config)


def validate_network_test_config(config: ProjectConfig, modules: tuple[JsTestModule, ...], network_enabled: bool) -> None:
    """Fail fast when network-only cases were requested without the needed credentials."""
    if not network_enabled:
        return

    module_names = {module.name for module in modules}
    missing: list[str] = []
    if "wifi" in module_names or "http" in module_names:
        if not config.test_wifi_ssid:
            missing.append("TEST_WIFI_SSID")
        if not config.test_wifi_password:
            missing.append("TEST_WIFI_PASSWORD")
    if "http" in module_names and not config.test_http_url:
        missing.append("TEST_HTTP_URL")

    if missing:
        joined = ", ".join(missing)
        raise SystemExit(
            f"Network JS tests require {joined}. Set them in `/.env` or the active mcu profile."
        )


def is_js_module_enabled(module: JsTestModule, runtime_features: dict[str, bool]) -> bool:
    """Check whether the runtime feature set supports a JS module."""
    return all(runtime_features.get(feature, False) for feature in module.required_features)


def resolve_js_modules_for_runtime(modules: tuple[JsTestModule, ...],
                                   runtime_features: dict[str, bool],
                                   explicit_selection: bool) -> tuple[tuple[JsTestModule, ...], str]:
    """Filter JS modules against the active mcu feature set."""
    if not runtime_features.get("fs", False):
        message = "mcu-backed JS tests require the fs feature because the harness is loaded from LittleFS"
        if explicit_selection:
            raise SystemExit(message)
        return (), message

    disabled_modules = tuple(
        module.name for module in modules if not is_js_module_enabled(module, runtime_features)
    )
    if explicit_selection and disabled_modules:
        joined = ", ".join(disabled_modules)
        raise SystemExit(f"Requested JS modules are disabled on this mcu: {joined}")

    enabled_modules = tuple(
        module for module in modules if is_js_module_enabled(module, runtime_features)
    )
    if disabled_modules:
        return enabled_modules, f"auto-skipped disabled modules: {','.join(disabled_modules)}"
    return enabled_modules, ""


def run_js_test_case(session: MonitorSession, case: JsTestCase) -> JsCaseResult:
    """Execute one JS test case file and collect a structured pass/fail result."""
    print(f"Running JS test {case.path}", flush=True)
    case_path = json.dumps(case.path)
    case_start_marker = f"{JS_TEST_CASE_START_PREFIX}{case.path}"
    command = (
        f"print({json.dumps(case_start_marker)}); "
        "try { "
        f"load({case_path}); "
        "} catch (error) { "
        'print("__TEST_FAIL__:" + JSON.stringify({'
        f"name: {case_path}, "
        "error: String(error)"
        "})); "
        "}"
    )
    try:
        send_js_command(session, command)
    except OSError as exc:
        message = f"unable to send command to monitor session ({exc})"
        print(f"FAIL {case.path}: {message}", flush=True)
        return JsCaseResult(case=case, case_name=case.path, status="failed", error=message)

    started_at = time.monotonic()
    deadline = started_at + case.timeout_seconds
    retry_deadline = started_at + JS_TEST_CASE_START_RETRY_SECONDS
    quiet_deadline: float | None = None
    output = ""
    result_line: str | None = None
    case_started = False
    command_retried = False

    while True:
        now = time.monotonic()
        if quiet_deadline is None and now >= deadline:
            break
        if quiet_deadline is not None and now >= quiet_deadline:
            break

        chunk = read_monitor_chunk(session)
        if chunk:
            output += chunk.decode("utf-8", errors="replace")
            normalized = normalize_serial_output(output)
            if find_last_output_line_with_prefix(
                normalized,
                case_start_marker,
                complete_only=True,
            ) is not None:
                case_started = True
            for line in normalized.splitlines():
                if (
                    line.startswith(JS_TEST_PASS_PREFIX) or
                    line.startswith(JS_TEST_SKIP_PREFIX) or
                    line.startswith(JS_TEST_FAIL_PREFIX)
                ):
                    result_line = line
                    quiet_deadline = time.monotonic() + 0.2
            continue

        if not case_started and not command_retried and now >= retry_deadline:
            print(
                f"Retrying JS test command for {case.path} after no start marker",
                flush=True,
            )
            try:
                send_js_command(session, command)
            except OSError as exc:
                message = f"unable to retry command to monitor session ({exc})"
                print(f"FAIL {case.path}: {message}", flush=True)
                return JsCaseResult(
                    case=case,
                    case_name=case.path,
                    status="failed",
                    error=message,
                )
            command_retried = True
            continue

        if session.process.poll() is not None:
            break

        time.sleep(0.05)

    if result_line is None:
        print(
            f"FAIL {case.path}: timed out waiting for a structured result\n"
            f"Last serial output:\n{format_output_tail(output)}",
            flush=True,
        )
        return JsCaseResult(
            case=case,
            case_name=case.path,
            status="failed",
            error="timed out waiting for a structured result",
        )

    if JS_TEST_FORBIDDEN_OUTPUT_MARKER in normalize_serial_output(output):
        message = "a handled Future rejection was reported again during finalization"
        print(
            f"FAIL {case.path}: {message}\n"
            f"Last serial output:\n{format_output_tail(output)}",
            flush=True,
        )
        return JsCaseResult(case=case, case_name=case.path, status="failed", error=message)

    if result_line.startswith(JS_TEST_FAIL_PREFIX):
        try:
            payload = json.loads(result_line[len(JS_TEST_FAIL_PREFIX):])
        except json.JSONDecodeError as exc:
            message = f"emitted an invalid failure payload ({exc})"
            print(
                f"FAIL {case.path}: {message}\n"
                f"Last serial output:\n{format_output_tail(output)}",
                flush=True,
            )
            return JsCaseResult(case=case, case_name=case.path, status="failed", error=message)

        case_name = payload.get("name", case.path)
        message = payload.get("error", "unknown error")
        print(
            f"FAIL {case_name}: {message}\n"
            f"Last serial output:\n{format_output_tail(output)}",
            flush=True,
        )
        return JsCaseResult(case=case, case_name=case_name, status="failed", error=message)

    if result_line.startswith(JS_TEST_SKIP_PREFIX):
        try:
            payload = json.loads(result_line[len(JS_TEST_SKIP_PREFIX):])
        except json.JSONDecodeError as exc:
            message = f"emitted an invalid skip payload ({exc})"
            print(
                f"FAIL {case.path}: {message}\n"
                f"Last serial output:\n{format_output_tail(output)}",
                flush=True,
            )
            return JsCaseResult(case=case, case_name=case.path, status="failed", error=message)

        case_name = payload.get("name", case.path)
        reason = payload.get("reason", "")
        reason_suffix = f" ({reason})" if reason else ""
        print(f"SKIP {case_name}{reason_suffix}", flush=True)
        return JsCaseResult(case=case, case_name=case_name, status="skipped", error=str(reason))

    try:
        payload = json.loads(result_line[len(JS_TEST_PASS_PREFIX):])
    except json.JSONDecodeError as exc:
        message = f"emitted an invalid pass payload ({exc})"
        print(
            f"FAIL {case.path}: {message}\n"
            f"Last serial output:\n{format_output_tail(output)}",
            flush=True,
        )
        return JsCaseResult(case=case, case_name=case.path, status="failed", error=message)

    case_name = payload.get("name", case.path)
    details = payload.get("details")
    details_suffix = (
        f" details={format_js_case_details(details)}"
        if case.record_details else ""
    )
    print(f"PASS {case_name}{details_suffix}", flush=True)
    return JsCaseResult(
        case=case,
        case_name=case_name,
        status="passed",
        details=details,
    )


def run_js_tests(config: ProjectConfig,
                 modules: tuple[JsTestModule, ...],
                 explicit_module_selection: bool,
                 enabled_capabilities: set[str],
                 flash_firmware_first: bool,
                 flash_fs_first: bool) -> TestStageSummary:
    """Flash the firmware and dedicated test image when needed, then drive JS tests over serial."""
    summary = TestStageSummary(
        name="JS",
        selection_label="modules",
        selection=tuple(module.name for module in modules),
    )
    js_config = js_test_build_config(
        config,
        modules=modules,
        explicit_module_selection=explicit_module_selection,
    )

    if flash_firmware_first:
        print("Flashing dedicated JS test firmware (leaving storage for the JS test image)", flush=True)
        try:
            flash(js_config, build_first=True, exclude_entries=("storage",))
        except subprocess.CalledProcessError as exc:
            summary.status = "failed"
            summary.note = f"firmware flash exited with code {exc.returncode}"
            print_test_stage_summary(summary)
            raise TestStageError(summary, "JS firmware flash failed.") from exc

    if flash_fs_first:
        print(f"Flashing JS test LittleFS image from {js_config.flash_data_override}", flush=True)
        try:
            flash_fs(js_config, build_first=True)
        except subprocess.CalledProcessError as exc:
            summary.status = "failed"
            summary.note = f"flash-fs exited with code {exc.returncode}"
            print_test_stage_summary(summary)
            raise TestStageError(summary, "JS test storage flash failed.") from exc

    if os.name == "nt":
        summary.status = "failed"
        summary.note = "mcu-backed JS tests require a POSIX host"
        print_test_stage_summary(summary)
        raise TestStageError(summary, "MCU-backed JS tests currently require a POSIX host because they run through a PTY monitor session.")

    session = start_monitor_session(js_config)
    try:
        try:
            startup_output = read_monitor_until_text(session, MONITOR_READY_MARKER, 10.0, "the ESP-IDF monitor banner")
            wait_for_optional_js_repl_banner(session, startup_output)
        except MarkerTimeoutError as exc:
            summary.status = "failed"
            summary.note = f"startup timeout waiting for {exc.description}"
            print_test_stage_summary(summary)
            raise TestStageError(
                summary,
                f"Timed out waiting for {exc.description}.\n"
                f"Last serial output:\n{format_output_tail(exc.output)}",
            ) from exc
        try:
            ensure_js_test_runtime(session)
        except MarkerTimeoutError as exc:
            summary.status = "failed"
            summary.note = f"startup timeout waiting for {exc.description}"
            print_test_stage_summary(summary)
            raise TestStageError(
                summary,
                f"Timed out waiting for {exc.description}.\n"
                f"Last serial output:\n{format_output_tail(exc.output)}",
            ) from exc
        try:
            runtime_features = probe_js_runtime_features(session)
        except MarkerTimeoutError as exc:
            summary.status = "failed"
            summary.note = f"feature probe timeout waiting for {exc.description}"
            print_test_stage_summary(summary)
            raise TestStageError(
                summary,
                f"Timed out waiting for {exc.description}.\n"
                f"Last serial output:\n{format_output_tail(exc.output)}",
            ) from exc
        try:
            selected_modules, selection_note = resolve_js_modules_for_runtime(
                modules,
                runtime_features,
                explicit_selection=explicit_module_selection,
            )
        except SystemExit as exc:
            summary.status = "failed"
            summary.note = str(exc)
            print_test_stage_summary(summary)
            raise TestStageError(summary, str(exc)) from exc

        summary.selection = tuple(module.name for module in selected_modules)
        summary.note = selection_note

        if not selected_modules:
            print_test_stage_summary(summary)
            return summary

        try:
            validate_network_test_config(config, selected_modules, "network" in enabled_capabilities)
        except SystemExit as exc:
            summary.status = "failed"
            summary.note = str(exc)
            print_test_stage_summary(summary)
            raise TestStageError(summary, str(exc)) from exc
        try:
            configure_js_test_runtime(session, js_config)
        except MarkerTimeoutError as exc:
            summary.status = "failed"
            summary.note = f"config timeout waiting for {exc.description}"
            print_test_stage_summary(summary)
            raise TestStageError(
                summary,
                f"Timed out waiting for {exc.description}.\n"
                f"Last serial output:\n{format_output_tail(exc.output)}",
            ) from exc

        for module in selected_modules:
            print(f"Running JS module {module.name}", flush=True)
            for case in module.cases:
                missing_capabilities = tuple(
                    capability for capability in case.required_capabilities
                    if capability not in enabled_capabilities
                )
                if missing_capabilities:
                    required_flags = describe_case_capabilities(missing_capabilities)
                    print(f"Skipping JS test {case.path} (requires {required_flags})", flush=True)
                    summary.skipped_cases += 1
                    continue
                if case.reset_before:
                    try:
                        reset_js_test_runtime(session, js_config)
                    except (MarkerTimeoutError, OSError) as exc:
                        message = f"failed to reset the JS runtime before isolated case ({exc})"
                        print(f"FAIL {case.path}: {message}", flush=True)
                        summary.failed_cases += 1
                        summary.status = "failed"
                        summary.failure_details.append(f"{case.path}: {message}")
                        continue
                collect_js_runtime(session)
                result = run_js_test_case(session, case)
                if case.reset_after:
                    try:
                        reset_js_test_runtime(session, js_config)
                    except (MarkerTimeoutError, OSError) as exc:
                        message = f"failed to reset the JS runtime after isolated case ({exc})"
                        print(f"FAIL {case.path}: {message}", flush=True)
                        summary.failed_cases += 1
                        summary.status = "failed"
                        summary.failure_details.append(f"{case.path}: {message}")
                        continue
                if result.status == "failed":
                    summary.failed_cases += 1
                    summary.status = "failed"
                    summary.failure_details.append(f"{result.case_name}: {result.error}")
                    continue
                if result.status == "skipped":
                    summary.skipped_cases += 1
                    continue
                summary.passed_cases += 1
        print_test_stage_summary(summary)
        if summary.status != "passed":
            raise TestStageError(summary, "JS tests failed. See the final report for failure details.")
        return summary
    finally:
        close_monitor_session(session)


def run_js_syntax_check(build_context_flash_data_dir: Path | None = None) -> None:
    """Parse framework tests, Build Context JavaScript, and documented examples."""
    command = [sys.executable, str(JS_SYNTAX_CHECK_SCRIPT)]
    if build_context_flash_data_dir is not None:
        command.extend(("--extra-path", str(build_context_flash_data_dir)))
    returncode, _ = run_streaming(command, cwd=ROOT_DIR)
    if returncode != 0:
        raise SystemExit("MQuickJS JavaScript syntax validation failed.")


def run_test_command(config: ProjectConfig, args: argparse.Namespace) -> None:
    """Run the selected host C and/or mcu JS automated tests."""
    scopes = resolve_test_scopes(args.scope)
    report = TestRunReport(scopes=scopes, stages=[])

    if "js" not in scopes:
        if args.module:
            raise SystemExit("`--module` requires JS scope.")
        if args.network:
            raise SystemExit("`--network` requires JS scope.")
        if args.loopback:
            raise SystemExit("`--loopback` requires JS scope.")
        if args.media_hardware:
            raise SystemExit("`--media-hardware` requires JS scope.")
        if args.wireless_hardware:
            raise SystemExit("`--wireless-hardware` requires JS scope.")
        if args.csi_hardware:
            raise SystemExit("`--csi-hardware` requires JS scope.")
        if args.csi_soak:
            raise SystemExit("`--csi-soak` requires JS scope.")
        if args.no_flash_firmware:
            raise SystemExit("`--no-flash-firmware` requires JS scope.")
        if args.no_flash_fs:
            raise SystemExit("`--no-flash-fs` requires JS scope.")

    if "js" in scopes:
        if args.csi_soak and not args.csi_hardware:
            raise SystemExit("`--csi-soak` also requires `--csi-hardware`.")
        run_js_syntax_check(config.build_context_flash_data_dir)

    stage_errors: list[str] = []

    if "c" in scopes:
        try:
            report.stages.append(run_host_c_tests())
        except TestStageError as exc:
            if not report.stages or report.stages[-1] is not exc.summary:
                report.stages.append(exc.summary)
            stage_errors.append(exc.message)

    if "js" in scopes:
        try:
            report.stages.append(
                run_js_tests(
                    config,
                    resolve_js_modules(args.module),
                    explicit_module_selection=bool(args.module),
                    enabled_capabilities=resolve_js_test_capabilities(args),
                    flash_firmware_first=not args.no_flash_firmware,
                    flash_fs_first=not args.no_flash_fs,
                )
            )
        except TestStageError as exc:
            if not report.stages or report.stages[-1] is not exc.summary:
                report.stages.append(exc.summary)
            stage_errors.append(exc.message)

    print_test_report(report)
    if stage_errors:
        raise SystemExit("One or more test stages failed.")
