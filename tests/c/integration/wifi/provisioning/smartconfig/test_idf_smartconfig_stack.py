"""Deferred ABI checks of the production SmartConfig cleanup machine code.

The small instruction decoders below execute the emitted store loops, not a
second SmartConfig state machine. Native window spill/interrupt and complete
decoder behavior still require the target stage. No tests run during codegen.
"""
from tests.support.paths import ROOT as TEST_ROOT
import os
from pathlib import Path
import struct
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = TEST_ROOT
sys.path.insert(0, str(ROOT / 'scripts'))
from patch_idf_smartconfig import patch_decoder_null_stores
from patch_idf_smartconfig_stack import REVIEWED, MEMBERS, _riscv_clear, _xtensa_clear, patch_archive


def signed(value, bits):
    return value - (1 << bits) if value & (1 << (bits - 1)) else value


def execute_riscv(code, registers, memory):
    pc = steps = 0
    while pc < len(code):
        steps += 1
        if steps > 1000:
            raise AssertionError('Nonterminating native clear loop')
        word, = struct.unpack_from('<I', code, pc)
        opcode, rd, rs1, rs2 = word & 127, (word >> 7) & 31, (word >> 15) & 31, (word >> 20) & 31
        next_pc = pc + 4
        if opcode == 0x13 and (word >> 12) & 7 == 0:  # ADDI
            registers[rd] = (registers[rs1] + signed(word >> 20, 12)) & 0xffffffff
        elif opcode == 0x23 and (word >> 12) & 7 == 2:  # SW
            offset = signed(((word >> 25) << 5) | ((word >> 7) & 31), 12)
            address = registers[rs1] + offset
            if not 0 <= address <= len(memory) - 4 or address % 4:
                raise AssertionError('Native clear escaped its backing stack')
            struct.pack_into('<I', memory, address, registers[rs2])
        elif opcode == 0x63 and (word >> 12) & 7 == 6:  # BLTU
            offset = ((word >> 31) << 12) | (((word >> 7) & 1) << 11) | (((word >> 25) & 63) << 5) | (((word >> 8) & 15) << 1)
            if registers[rs1] < registers[rs2]:
                next_pc = pc + signed(offset, 13)
        else:
            raise AssertionError('Unexpected native instruction: ' + hex(word))
        if registers[0] != 0 or next_pc < 0 or next_pc % 4:
            raise AssertionError('Invalid native register/branch')
        pc = next_pc


def execute_xtensa(code, registers, memory):
    pc = steps = 0
    while pc < len(code):
        steps += 1
        if steps > 1000:
            raise AssertionError('Nonterminating native clear loop')
        word = int.from_bytes(code[pc:pc + 3], 'little')
        short = word & 0xffff
        if short == 0xf01d:  # RETW.N; window behavior is a target test
            return
        if word & 0xf00f == 0xa002:  # MOVI
            target = (word >> 4) & 15
            immediate = (word >> 16) | (((word >> 8) & 15) << 8)
            registers[target] = signed(immediate, 12)
            pc += 3
        elif short & 0xf00f == 0x000d:  # MOV.N
            registers[(short >> 4) & 15] = registers[(short >> 8) & 15]
            pc += 2
        elif short & 15 == 9:  # S32I.N
            address = registers[(short >> 8) & 15] + ((short >> 12) & 15) * 4
            if not 0 <= address <= len(memory) - 4 or address % 4:
                raise AssertionError('Native clear escaped its backing stack')
            struct.pack_into('<I', memory, address, registers[(short >> 4) & 15])
            pc += 2
        elif short & 15 == 11:  # ADDI.N (immediate 0 encodes -1)
            immediate = (short >> 4) & 15
            registers[(short >> 12) & 15] = registers[(short >> 8) & 15] + (immediate if immediate else -1)
            pc += 2
        elif word & 0xf00f == 0xc002:  # ADDI
            registers[(word >> 4) & 15] = registers[(word >> 8) & 15] + signed(word >> 16, 8)
            pc += 3
        elif word & 255 == 0x56:  # BNEZ, PC-relative from instruction + 4
            pc = pc + 4 + signed(word >> 12, 12) if registers[(word >> 8) & 15] else pc + 3
        else:
            raise AssertionError('Unexpected Xtensa native instruction: ' + hex(word))
    raise AssertionError('Native loop lost its return')


class SmartConfigDecoderStack(unittest.TestCase):
    def test_relaxed_native_links_enter_cleanup_then_resume_the_displaced_exit(self):
        if not os.environ.get('IDF_PATH'):
            self.skipTest('IDF_PATH must name the reviewed SDK')
        ld = shutil.which('riscv32-esp-elf-ld')
        if not ld:
            tools_root = Path(os.environ.get('IDF_TOOLS_PATH', str(Path.home() / '.espressif')))
            candidates = sorted(tools_root.glob('tools/riscv32-esp-elf/*/*/bin/riscv32-esp-elf-ld'))
            if not candidates:
                self.skipTest('The SDK RISC-V linker is required')
            ld = str(candidates[-1])
        ar = ld[:-2] + 'ar'
        sdk = Path(os.environ['IDF_PATH']) / 'components/esp_wifi/lib'
        for target in ('esp32c3', 'esp32c5'):
            with self.subTest(target=target), tempfile.TemporaryDirectory() as temp:
                directory = Path(temp)
                original = (sdk / target / 'libsmartconfig.a').read_bytes()
                archive = directory / 'libsmartconfig.a'
                archive.write_bytes(patch_archive(patch_decoder_null_stores(target, original), target))
                objects = []
                for member in MEMBERS:
                    path = directory / member
                    path.write_bytes(subprocess.check_output([ar, 'p', str(archive), member]))
                    objects.append(str(path))
                elf = directory / 'decoder.elf'
                # Relocation-only image; native external calls are not executed.
                subprocess.run([ld, '--relax', '--unresolved-symbols=ignore-all', '--no-gc-sections',
                                '-e', 'sc_get_ssid_passwd', '-Ttext=0x42000000', '-o', str(elf)] + objects,
                               check=True, capture_output=True)
                data = elf.read_bytes()
                shoff = struct.unpack_from('<I', data, 32)[0]
                width, count, names_index = struct.unpack_from('<HHH', data, 46)
                sections = [struct.unpack_from('<10I', data, shoff + i * width) for i in range(count)]
                names = data[sections[names_index][4]:][:sections[names_index][5]]
                text = next(s for s in sections if names[s[0]:names.index(b'\0', s[0])] == b'.text')
                code = data[text[4]:][:text[5]]
                def jump(pc, word):
                    offset = ((word >> 31) << 20) | (((word >> 12) & 255) << 12) | (((word >> 20) & 1) << 11) | (((word >> 21) & 1023) << 1)
                    return pc + signed(offset, 21)
                for _, length, sites in REVIEWED[target].values():
                    loop = _riscv_clear(length)
                    positions = [i for i in range(len(code)) if code.startswith(loop, i)]
                    self.assertEqual(len(positions), len(sites))
                    for start in positions:
                        incoming = [pc for pc in range(0, len(code) - 3, 2)
                                    if struct.unpack_from('<I', code, pc)[0] & 0xfff == 0x6f
                                    and jump(pc, struct.unpack_from('<I', code, pc)[0]) == start]
                        self.assertEqual(len(incoming), 1)
                        word = struct.unpack_from('<I', code, start + 24)[0]
                        self.assertEqual(word & 0xfff, 0x6f)
                        self.assertEqual(jump(start + 24, word), incoming[0] + 4)

    def test_production_loops_erase_locals_preserve_return_registers_and_frame_edges(self):
        for target, functions in REVIEWED.items():
            for function, (_, length, _) in functions.items():
                with self.subTest(target=target, function=function):
                    stack = bytearray([0xa5] * 512)
                    regs = [0x11220000 + i for i in range(32)]
                    if target == 'esp32s3':
                        regs[1] = 32
                        preserved = tuple(range(8))
                        execute, loop = execute_xtensa, _xtensa_clear(length)
                    else:
                        regs[0], regs[2] = 0, 32
                        preserved = tuple(i for i in range(32) if i not in (5, 6))
                        execute, loop = execute_riscv, _riscv_clear(length)
                    before = list(regs)
                    execute(loop, regs, stack)
                    self.assertEqual(stack[:32], b'\xa5' * 32)
                    self.assertEqual(stack[32:32 + length], bytes(length))
                    self.assertEqual(stack[32 + length:], b'\xa5' * (480 - length))
                    for index in preserved:
                        self.assertEqual(regs[index], before[index])

    def test_production_transformer_rejects_repeated_or_unreviewed_decoder_sections(self):
        if not os.environ.get('IDF_PATH'):
            self.skipTest('IDF_PATH must name the reviewed SDK')
        sdk = Path(os.environ['IDF_PATH']) / 'components/esp_wifi/lib'
        for target in REVIEWED:
            original = (sdk / target / 'libsmartconfig.a').read_bytes()
            prepared = patch_decoder_null_stores(target, original)
            patched = patch_archive(prepared, target)
            with self.assertRaises(ValueError):
                patch_archive(patched, target)
            # Drift in an actual credential-bearing section is rejected.
            from patch_idf_twt_probe_wake import patch_archive as rewrite_archive
            def corrupt(data):
                shoff = struct.unpack_from('<I', data, 32)[0]
                width, count, names_index = struct.unpack_from('<HHH', data, 46)
                sections = [struct.unpack_from('<10I', data, shoff + i * width) for i in range(count)]
                names = data[sections[names_index][4]:][:sections[names_index][5]]
                changed = bytearray(data)
                for section in sections:
                    name = names[section[0]:names.index(b'\0', section[0])]
                    if name == b'.text.sc_get_ssid_passwd':
                        changed[section[4] + section[5] - 1] ^= 1
                        return bytes(changed)
                raise AssertionError('Missing real decoder section')
            drift = rewrite_archive(prepared, member_patches={'sc_esptouch_v2.o': corrupt})
            with self.assertRaises(ValueError):
                patch_archive(drift, target)
