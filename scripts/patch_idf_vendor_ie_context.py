"""Repair the pinned SDK Vendor IE callback context in a build-local archive.

The reviewed API wrapper writes ctx to message+20, but its ioctl handler stores
message+20 itself in g_ic+204. The RX call site passes g_ic+204 without dereference.
Replace that one address instruction with an equal-sized word load. Exact archive
hash and unique complete function bytes gate the base instruction changes;
symbol/member offsets and relocations remain identical for those repairs.
The optional C5 probe PM fix appends undefined symbols, retargets six exact CALL
relocations, and rebuilds GNU archive offsets; code/data and old symbol indices
remain unchanged. Never patch shared IDF.

The same reviewed archive hashes also gate the Action/ROC cancellation wrappers
and their private g_offchan_ctx layout/retirement probe. Review the full record
clear ordering and both adapters before changing these hashes. The C5 hash also
gates the saved-PHY ioctl layout and read-only protocol/bandwidth handlers.
It also gates the TWT native table snapshot and setup admission wrappers; review
their pending-slot/flow/ID checks and legacy timer layouts before updating it.
With FTM enabled, also repair the public report getter rejecting the documented
NULL discard request. Retain its branch relocation and target, but test the
already-loaded nonzero INVALID_ARG register, permitting the native NULL path.
"""
from __future__ import annotations
import argparse
import hashlib
from pathlib import Path
from patch_idf_twt_probe_wake import patch_archive as patch_probe_wake_archive
from patch_idf_offchan_frame import patch_archive as patch_offchan_frame_archive
from patch_idf_nan_sd import patch_archive as patch_nan_sd_archive, validate_pairing_tx_archive

REVIEWED = {
    'esp32c3': '0fcbed322d3254063b2c191bc8b8004e703ffd640296164a6552676180909a0c',
    'esp32s3': '265e5c89ac0d2a9444b5b466afee49f631ec549f96774f91a721ab5069c58e20',
    'esp32c5': '4c86fc1d2f40a933af7f672972978eb7c82651d838fd10584e006438d585793b',
}
RISCV_FUNCTION = bytes.fromhex('5845b707000093870700510523a6a70c23a4e70c01458280')
XTENSA_FUNCTION = bytes.fromhex('00000000364100983281fdff22c2142268339268320c021df0')
# wl_cnx.o's sole undefined itwt_probe_rc_tx_cb call normally loses EB identity.
# Keep the complete function, all relocations and archive offsets; pass its
# saved EB (s0) at the same-width instruction before the wrapped call instead.
C5_PROBE_CALLBACK = bytes.fromhex('5c41411122c42a84c84383578402096706c6f98f91c32105410597000000e780000011c9185c834637010547639ae6021547a304e50a1c5c03c5370197000000e780000005cd97000000e7800000814519cd2244b240410117030000670003000546f55597000000e7800000e9b72244b24041011703000067000300b240224441018280')

# Complete reviewed esp_wifi_ftm_get_report sections, including S3 literals.
# The wrapper has already checked initialization/driver state. Immediately
# before its NULL rejection branch it loads ESP_ERR_INVALID_ARG (0x102) into
# a0 (RISC-V) / a8 (Xtensa). Test that known nonzero register instead of the
# caller pointer. The branch remains a branch: its original relocation is
# preserved and cannot turn an inserted NOP into an invalid instruction.
FTM_REPORT_FUNCTIONS = {
    'esp32c3': bytes.fromhex('797122d426d206d62a84ae8497000000e780000029c1b707000003c7971e854763fee7021305201015c0b7070000938707003eca85457c00480822c6230891003ecc02ce97000000e7800000b25022549254456182800d650505cdbf0d650905f5b7'),
    'esp32c5': bytes.fromhex('797122d426d206d62a84ae8497000000e780000029c1b707000003c72720854763fee7021305201015c0b7070000938707003eca85457c00480822c6230891003ecc02ce97000000e7800000b25022549254456182800d650505cdbf0d650905f5b7'),
    'esp32s3': bytes.fromhex('01300000023000000000000000000000000000000000000036810081fdffe0080081f7ff303074bc1a81f7ff82d8019208e981f4ffb6292382a1029cd2293121f3ff0c1b2901cb212911ad010c0232411022610281f0ffe008008d0a2d081df0'),
}

# The two native restore wrappers overwrite wifi_nvs_load(true)'s result with
# ESP_OK, even on allocation failure. Keep the SDK loader, Wi-Fi task/IPC and
# every original state reference; replace only the two return-value writes.
# Lower-level NVS setter/commit failures are still not reported by the loader.
RESTORE_FUNCTIONS = {
    'esp32c3': (
        (bytes.fromhex('4111054506c697000000e7800000b240014541018280'), 0x10),
        (bytes.fromhex('411106c697000000e780000097000000e7800000b240014541018280'), 0x16),
    ),
    'esp32c5': (
        (bytes.fromhex('4111054506c697000000e7800000b240014541018280'), 0x10),
        (bytes.fromhex('411106c697000000e780000097000000e7800000b240014541018280'), 0x16),
    ),
    'esp32s3': (
        (bytes.fromhex('54000000364100a2a00181fdffe008000c021df0'), 0x10),
        (bytes.fromhex('000000000000000036410081fdffe0080081fcffe008000c021df0'), 0x17),
    ),
}


def patch_archive(source: bytes, target: str, *, ftm_report_null_fix: bool = False,
                  twt_probe_buffer_fix: bool = False, twt_probe_wake_fix: bool = False,
                  offchan_frame_fix: bool = False, nan_sd_buffer_fix: bool = False) -> bytes:
    digest = hashlib.sha256(source).hexdigest()
    if target not in REVIEWED or digest != REVIEWED[target]:
        raise ValueError(f'Unreviewed {target} libnet80211.a SHA-256 {digest}; '
                         'review Vendor IE context and Action/ROC retirement before updating this fix')
    if nan_sd_buffer_fix:
        validate_pairing_tx_archive(source)
    before = XTENSA_FUNCTION if target == 'esp32s3' else RISCV_FUNCTION
    # S3: addi a2,a2,20 -> l32i a2,a2,20 (3 bytes).
    # C3/C5: c.addi a0,20 -> c.lw a0,20(a0) (2 bytes).
    offset, instruction = (12, bytes.fromhex('222205')) if target == 'esp32s3' else (10, bytes.fromhex('4849'))
    if source.count(before) != 1:
        raise ValueError('Expected one reviewed wifi_set_vnd_ie_cb_process section')
    after = before[:offset] + instruction + before[offset + len(instruction):]
    patched = source.replace(before, after, 1)
    for before, offset in RESTORE_FUNCTIONS[target]:
        if patched.count(before) != 1:
            raise ValueError('Expected one reviewed native Wi-Fi restore wrapper')
        # RISC-V: c.nop preserves a0. Xtensa windowed ABI: mov.n a2,a10
        # returns the child call's value. No relocation touches either write.
        instruction = bytes.fromhex('2d0a') if target == 'esp32s3' else bytes.fromhex('0100')
        after = before[:offset] + instruction + before[offset + 2:]
        patched = patched.replace(before, after, 1)
    if ftm_report_null_fix:
        before = FTM_REPORT_FUNCTIONS[target]
        if source.count(before) != 1:
            raise ValueError('Expected one reviewed esp_wifi_ftm_get_report section')
        # C3/C5 c.beqz s0 -> c.beqz a0; S3 beqz.n a2 -> beqz.n a8.
        offset, branch = (0x3b, bytes.fromhex('9cd8')) if target == 'esp32s3' else (0x28, bytes.fromhex('15c1'))
        after = before[:offset] + branch + before[offset + len(branch):]
        patched = patched.replace(before, after, 1)
    if twt_probe_buffer_fix:
        if target != 'esp32c5' or source.count(C5_PROBE_CALLBACK) != 1:
            raise ValueError('Expected one reviewed C5 cnx_probe_rc_tx_cb section')
        before = C5_PROBE_CALLBACK
        # lbu a0,19(a5) -> addi a0,s0,0; no relocation touches these four bytes.
        after = before[:0x38] + bytes.fromhex('13050400') + before[0x3c:]
        patched = patched.replace(before, after, 1)
    if twt_probe_wake_fix:
        if target != 'esp32c5':
            raise ValueError('Probe PM fix requires the reviewed C5 archive')
        patched = patch_probe_wake_archive(patched)
    if offchan_frame_fix:
        patched = patch_offchan_frame_archive(patched, target)
    if nan_sd_buffer_fix:
        patched = patch_nan_sd_archive(patched, target)
    return patched


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--target', required=True)
    parser.add_argument('--ftm-report-null-fix', action='store_true')
    parser.add_argument('--twt-probe-buffer-fix', action='store_true')
    parser.add_argument('--twt-probe-wake-fix', action='store_true')
    parser.add_argument('--offchan-frame-fix', action='store_true')
    parser.add_argument('--nan-sd-buffer-fix', action='store_true')
    args = parser.parse_args()
    if args.source.resolve() == args.output.resolve():
        parser.error('output must be a build-local archive, not the SDK source')
    try:
        patched = patch_archive(args.source.read_bytes(), args.target, ftm_report_null_fix=args.ftm_report_null_fix,
                                twt_probe_buffer_fix=args.twt_probe_buffer_fix, twt_probe_wake_fix=args.twt_probe_wake_fix,
                                offchan_frame_fix=args.offchan_frame_fix, nan_sd_buffer_fix=args.nan_sd_buffer_fix)
    except ValueError as exc:
        parser.error(str(exc))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    if not args.output.exists() or args.output.read_bytes() != patched:
        args.output.write_bytes(patched)
    print('ESP32QJS Wi-Fi restore: preserve native default-loader errors through both SDK wrappers')
    if args.ftm_report_null_fix:
        print('ESP32QJS FTM report fix: documented NULL discard enabled; native guards/relocations preserved')
    if args.twt_probe_buffer_fix:
        print('ESP32QJS TWT probe callback: exact EB passed to identity guard; native connection path preserved')
    if args.twt_probe_wake_fix:
        print('ESP32QJS TWT probe PM: six exact call relocations use independent wake ownership')
    if args.offchan_frame_fix:
        print('ESP32QJS off-channel TX: actual frame argument, native output/reset and recycle identity')
        print('ESP32QJS CHM timeout: native record reset invalidates both arm tickets')
    print('ESP32QJS Vendor IE context fix: reviewed build-local archive '
          + hashlib.sha256(patched).hexdigest())


if __name__ == '__main__':
    main()
