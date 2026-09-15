"""Command-line parsing and top-level ESP32QJS development command dispatch."""

from __future__ import annotations

import argparse
import os
import sys

from .build import build, build_fs_image
from .device_tests import JS_TEST_MODULES, resolve_js_modules, run_js_syntax_check, run_test_command
from .flash import chip_id, flash, flash_fs, flash_monitor, flash_workspace
from .profiles import (
    BuildContextProfile,
    MCUProfile,
    ProjectConfig,
    build_project_config,
    format_path,
    list_mcus,
    load_build_context,
    load_profile,
    show_config,
)
from .server import monitor, start_server


def add_common_mcu_args(parser: argparse.ArgumentParser, profile: MCUProfile) -> None:
    """Attach MCU tool options derived from the selected Build Context."""
    parser.add_argument(
        "--build-dir",
        default=format_path(profile.build_dir),
        help="Build subdirectory under build/.",
    )
    parser.add_argument(
        "--allow-external-build-dir",
        action="store_true",
        help="Explicitly allow an absolute build directory outside the project build/ root.",
    )
    parser.add_argument(
        "--sdkconfig-defaults",
        default=format_path(profile.sdkconfig_defaults),
        help="Optional MCU/base sdkconfig defaults applied before the Build Context.",
    )
    parser.add_argument("--idf-path", default=profile.idf_path)
    parser.set_defaults(mcu_file=str(profile.file))


def add_common_build_context_args(
    parser: argparse.ArgumentParser,
    profile: BuildContextProfile,
) -> None:
    """Expose only the fixed Build Context root as firmware build input."""
    parser.add_argument(
        "--build-context",
        default=profile.reference,
        help="Resolved immutable Build Context directory produced by the Hub.",
    )


def add_common_connection_args(parser: argparse.ArgumentParser, profile: MCUProfile) -> None:
    """Attach the shared device-target and tool overrides once at the top level."""
    parser.add_argument(
        "--target",
        default=profile.target,
        help="Device target for chip-id/flash/monitor/test/server. Use an rfc2217:// URL for remote mcus, or a local serial device path such as /dev/ttyACM0 or COM3.",
    )
    parser.add_argument("--baud", type=int, default=profile.monitor_baud)
    parser.add_argument("--esptool-bin", default=profile.esptool_bin)
    parser.add_argument("--listen-port", type=int, default=profile.listen_port)
    parser.add_argument("--python-exe", default=profile.server_python_exe)


def parse_args(
    argv: list[str] | None = None,
) -> tuple[argparse.Namespace, MCUProfile, BuildContextProfile]:
    """Parse CLI arguments after resolving the immutable Build Context."""
    raw_argv = sys.argv[1:] if argv is None else argv
    bootstrap = argparse.ArgumentParser(add_help=False)
    bootstrap.add_argument("--build-context")
    pre_args, _ = bootstrap.parse_known_args(raw_argv)
    build_context = load_build_context(pre_args.build_context)
    profile = load_profile(build_context.mcu)

    parser = argparse.ArgumentParser(description="ESP32QJS firmware helper for a resolved Build Context.")
    add_common_mcu_args(parser, profile)
    add_common_build_context_args(parser, build_context)
    add_common_connection_args(parser, profile)
    parser.add_argument(
        "--assume",
        choices=("ask", "y", "n"),
        default="ask",
        help="Skip manual prompts by answering yes/no automatically. Default: ask.",
    )

    sub = parser.add_subparsers(dest="command", required=True)

    mcus_parser = sub.add_parser("mcus", help="List bundled mcu profiles.")
    mcus_parser.set_defaults(_noop=True)

    show_parser = sub.add_parser("show-config", help="Print the MCU/Build Context/tool configuration.")
    show_parser.set_defaults(_noop=True)

    server = sub.add_parser("server", help="Write config and start esp_rfc2217_server.")
    server.add_argument("--force-restart", action="store_true")

    build_parser = sub.add_parser("build", help="Run idf.py build for the selected mcu.")
    build_parser.set_defaults(_noop=True)

    build_fs_parser = sub.add_parser("build-fs", help="Build only the LittleFS storage image.")
    build_fs_parser.set_defaults(_noop=True)

    check_js_parser = sub.add_parser(
        "check-js",
        help="Parse all first-party JavaScript with the exact MQuickJS engine.",
    )
    check_js_parser.set_defaults(_noop=True)

    chip = sub.add_parser("chip-id", help="Check connectivity over the selected target.")

    flash_parser = sub.add_parser("flash", help="Build and flash over the selected target.")
    flash_parser.add_argument("--no-build", action="store_true")
    flash_parser.add_argument(
        "--erase-workspace",
        action="store_true",
        help="Also flash the generated empty workspace image. Default: preserve workspace.",
    )

    flash_fs_parser = sub.add_parser("flash-fs", help="Build and flash only the LittleFS storage partition.")
    flash_fs_parser.add_argument("--no-build", action="store_true")

    flash_workspace_parser = sub.add_parser(
        "flash-workspace",
        help="Explicitly erase and initialize only the workspace partition.",
    )
    flash_workspace_parser.add_argument("--no-build", action="store_true")

    mon = sub.add_parser("monitor", help="Open ESP-IDF monitor over the selected target.")

    fm = sub.add_parser("flash-monitor", help="Build/flash, then open monitor over the selected target.")
    fm.add_argument("--no-build", action="store_true")
    fm.add_argument(
        "--erase-workspace",
        action="store_true",
        help="Also flash the generated empty workspace image. Default: preserve workspace.",
    )

    test = sub.add_parser("test", help="Run host C tests and/or mcu-backed JS tests.")
    test.add_argument(
        "--scope",
        action="append",
        choices=("c", "js"),
        help="Limit test stages. Repeat to include both. Default: run C and JS.",
    )
    test.add_argument(
        "--module",
        action="append",
        choices=[module.name for module in JS_TEST_MODULES],
        help="Limit JS tests to specific modules. Default: run mcu-enabled modules only.",
    )
    test.add_argument(
        "--network",
        action="store_true",
        help="Enable network-required JS cases inside the wifi/http modules.",
    )
    test.add_argument(
        "--loopback",
        action="store_true",
        help="Enable JS cases that require physical loopback wiring, such as SPI MOSI-to-MISO or UART TX-to-RX validation.",
    )
    test.add_argument(
        "--media-hardware",
        action="store_true",
        help="Enable JS cases that require media support in the Build Context and attached camera or microphone hardware.",
    )
    test.add_argument(
        "--wireless-hardware",
        action="store_true",
        help="Enable isolated on-device Wi-Fi/ESP-NOW radio lifecycle E2E cases.",
    )
    test.add_argument(
        "--csi-hardware",
        action="store_true",
        help="Enable Wi-Fi CSI RF capture and batch-transport hardware cases.",
    )
    test.add_argument(
        "--csi-soak",
        action="store_true",
        help="Enable the additional 1-60 minute Wi-Fi CSI memory soak; also pass --csi-hardware.",
    )
    test.add_argument(
        "--no-flash-firmware",
        action="store_true",
        help="Reuse the existing firmware instead of rebuilding and reflashing it before JS tests.",
    )
    test.add_argument(
        "--no-flash-fs",
        action="store_true",
        help="Reuse the existing JS test LittleFS image instead of rebuilding and reflashing it.",
    )

    return parser.parse_args(raw_argv), profile, build_context


def main() -> int:
    args, profile, build_context = parse_args()
    config = build_project_config(args, profile, build_context)

    if args.command == "mcus":
        list_mcus(profile.reference)
        return 0

    if args.command == "show-config":
        show_config(config)
        return 0

    if args.command == "server":
        return start_server(config, args.force_restart)

    if args.command == "build":
        build(config)
        return 0

    if args.command == "build-fs":
        build_fs_image(config)
        return 0

    if args.command == "check-js":
        run_js_syntax_check(config.build_context_flash_data_dir)
        return 0

    if args.command == "chip-id":
        chip_id(config)
        return 0

    if args.command == "flash":
        flash(
            config,
            build_first=not args.no_build,
            initialize_workspace=args.erase_workspace,
        )
        return 0

    if args.command == "flash-fs":
        flash_fs(config, build_first=not args.no_build)
        return 0

    if args.command == "flash-workspace":
        flash_workspace(config, build_first=not args.no_build)
        return 0

    if args.command == "monitor":
        monitor(config)
        return 0

    if args.command == "flash-monitor":
        flash_monitor(
            config,
            build_first=not args.no_build,
            initialize_workspace=args.erase_workspace,
        )
        return 0

    if args.command == "test":
        run_test_command(config, args)
        return 0

    return 2
