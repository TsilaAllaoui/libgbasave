#!/usr/bin/env python3
from __future__ import annotations
import pathlib, subprocess, tempfile, re, struct

ROOT = pathlib.Path(__file__).resolve().parents[1]
from test_support import GBASAVEHANDLER_BIN as BIN


def put(data: bytearray, off: int, hexs: str):
    raw = bytes.fromhex(hexs)
    data[off:off + len(raw)] = raw


def base_rom(marker: bytes) -> bytearray:
    data = bytearray(0x100000)
    struct.pack_into('<I', data, 0x0000, 0xEA00002E)  # standard ARM reset branch -> 0x080000C0
    data[0xA0:0xAC] = b'GBASH TEST   '
    data[0xAC:0xB0] = b'TEST'
    data[0x1000:0x1000 + len(marker)] = marker
    return data


def run_patch(data: bytearray, save_type: str) -> tuple[bytes, str]:
    with tempfile.TemporaryDirectory() as td:
        td = pathlib.Path(td)
        src, out = td / 'in.gba', td / 'out.gba'
        src.write_bytes(data)
        cp = subprocess.run(
            [str(BIN), 'patch', str(src), str(out), '--nor-profile','m6m','--save-type', save_type],
            check=True, text=True, stdout=subprocess.PIPE)
        return out.read_bytes(), cp.stdout


def blocks(text: str):
    return [(int(a, 16), int(b, 16), src)
            for a, b, src in re.findall(r'block \d+ -> 0x([0-9A-F]+)\.\.0x([0-9A-F]+) (\S+)', text)]


def assert_fresh_block(out: bytes, start: int, kind: str, block_bytes: int = 0x10000):
    anchor = start + block_bytes - (8 if kind == 'eeprom' else 4)
    assert out[start:anchor] == b'\xff' * (anchor - start)
    assert out[anchor:anchor + 2] == b'\xAA\xAA'


def flash512_fixture(marker: bytes = b'FLASH512_V131'):
    d = base_rom(marker)
    # Physical primitives beneath Nintendo's common public FLASH wrappers.
    put(d, 0x1800, '30B591B0684600F0')
    put(d, 0x2000, '10B5041C531E002A')
    put(d, 0x2800, '30B5051C0B1C541E')
    put(d, 0x4000, '70B590B0154D2988')
    put(d, 0x5000, '70B5464640B490B0')
    put(d, 0x6000, 'F0B590B00F1C0004040C034800684089844205D3014840E00000FF800000201CFFF7D7FE0004050C002D35D1')
    return d


def eeprom_fixture(marker: bytes):
    d = base_rom(marker)
    if marker in (b'EEPROM_V120', b'EEPROM_V121', b'EEPROM_V122'):
        put(d, 0x1FFE, '70B5')
        put(d, 0x2000, 'A2B00D1C0004030C034800688088834205D3014877E0')
        put(d, 0x3000, '30B5A9B00D1C0004040C034800688088844205D3014855E0')
    elif marker == b'EEPROM_V124':
        put(d, 0x1FFE, '70B5')
        put(d, 0x2000, 'A2B00D1C0004030C034800688088834205D3014877E0')
        put(d, 0x3000, 'F0B5ACB00D1C0004010C1206170E034800688088814205D3')
    else:
        put(d, 0x1FFE, '70B5')
        put(d, 0x2000, 'A2B00D1C0004030C034800688088834205D301484AE0')
        put(d, 0x3000, 'F0B5474680B4ACB00E1C0004050C1206120E904603480068')
    return d


def sram_fixture(marker: bytes = b'SRAM_V110'):
    d = base_rom(marker)

    read_core = 0x17C0
    read_wrapper = 0x1800
    writer = 0x2000
    verify_core = 0x27C0
    verify_wrapper = 0x2800

    # The wrappers are stock and copy their tiny physical cores from ROM.
    put(d, read_core, '0123456789ABCDEF0123456789ABCDEF')
    put(d, read_wrapper, '70B5A0B0041C0D1C161C084A10880849')
    struct.pack_into('<I', d, read_wrapper + 0x34, 0x08000000 + read_core + 1)

    put(d, writer, '30B5051C0C1C131C0B4A10880B490840')

    put(d, verify_core, '1032547698BADCFE1032547698BADCFE')
    put(d, verify_wrapper, '70B5B0B0041C0D1C161C084A10880849')
    struct.pack_into('<I', d, verify_wrapper + 0x34, 0x08000000 + verify_core + 1)

    # Standard Nintendo ARM IRQ dispatcher discovery pattern.
    struct.pack_into('<I', d, 0x00D8, 0xE59F1154)  # ldr r1,[pc,#0x154]
    struct.pack_into('<I', d, 0x00DC, 0xE28F0018)  # add r0,pc,#0x18 -> 0xFC
    struct.pack_into('<I', d, 0x00E0, 0xE5810000)  # str r0,[r1]
    struct.pack_into('<I', d, 0x00FC, 0xE3A03301)  # mov r3,#0x04000000
    struct.pack_into('<I', d, 0x0234, 0x03007FFC)

    # One real Thumb LDR literal whose value points into SRAM.
    struct.pack_into('<H', d, 0x4000, 0x4800)  # LDR r0,[pc,#0]
    struct.pack_into('<I', d, 0x4004, 0x0E000120)
    return d


def main():
    for marker in (b'FLASH512_V130', b'FLASH512_V131', b'FLASH512_V133'):
        source = flash512_fixture(marker)
        out, text = run_patch(source, 'flash512')
        bs = blocks(text)
        # CompactV2 uses program-only arenas sized from the 4 KiB logical
        # FLASH sector rather than the NOR erase geometry.  Require at least
        # three power-loss-safe generations without coupling the test to a
        # particular cartridge erase-unit size.
        versions = re.search(r'FLASH versions/block:\s+(\d+)', text)
        assert versions and int(versions.group(1)) >= 3
        assert 'FLASH spill blocks:    8' in text
        assert 'NOR wear policy:' in text
        assert len(bs) == 24
        for a, b, _ in bs:
            assert b - a + 1 >= 3 * 0x1000 + 0x100
            assert (b - a + 1) & (b - a) == 0, 'compact program arena must be power-of-two sized'
            assert_fresh_block(out, a, 'flash', b - a + 1)
        # All six validated physical primitives were redirected.
        assert 'Patched routines: 6' in text

    for marker in (b'EEPROM_V120', b'EEPROM_V121', b'EEPROM_V122', b'EEPROM_V124', b'EEPROM_V126'):
        out, text = run_patch(eeprom_fixture(marker), 'eeprom')
        bs = blocks(text)
        assert 'EEPROM8K' in text and 'Storage mode:  EEPROM_COMPACT_AB' in text and 'EEPROM records/dword:  6' in text and len(bs) == 2
        assert 'Patched routines: 2' in text
        for a, _, _ in bs:
            assert_fresh_block(out, a, 'eeprom')

    out, text = run_patch(eeprom_fixture(b'EEPROM_V122'), 'eeprom512')
    bs = blocks(text)
    assert 'EEPROM512' in text and len(bs) == 1
    assert_fresh_block(out, bs[0][0], 'eeprom')

    for marker in (b'SRAM_V110', b'SRAM_V111', b'SRAM_V112', b'SRAM_V113',
                   b'SRAM_F_V100', b'SRAM_F_V102', b'SRAM_F_V103', b'SRAM_F_V110'):
        source = sram_fixture(marker)
        out, text = run_patch(source, 'sram')
        bs = blocks(text)
        assert 'FIXED_SRAM_MIRROR' in text and len(bs) == 2
        assert 'SRAM live state:       RAM/EWRAM only during gameplay' in text
        assert 'SRAM NOR retention:    restore/reload + changed hotkey commit only' in text
        assert 'SRAM direct literals:  1' in text
        assert 'NOR wear policy: normal gameplay is RAM/EWRAM-only; NOR changes only during restore/explicit changed-save retention commits.' in text
        assert 'Patched routines: 5' in text
        for a, _, _ in bs:
            assert_fresh_block(out, a, 'sram')
        assert struct.unpack_from('<I', out, 0x4004)[0] == 0x02027000 + 0x120
        # Public wrappers remain stock; copied cores and direct writer change.
        assert out[0x1800:0x1808] == source[0x1800:0x1808]
        assert out[0x2800:0x2808] == source[0x2800:0x2808]
        assert out[0x17C0:0x17C8] != source[0x17C0:0x17C8]
        assert out[0x2000:0x2008] != source[0x2000:0x2008]
        assert out[0x27C0:0x27C8] != source[0x27C0:0x27C8]

    v111 = base_rom(b'EEPROM_V111')
    with tempfile.TemporaryDirectory() as td:
        td = pathlib.Path(td)
        src, out = td / 'in.gba', td / 'out.gba'
        src.write_bytes(v111)
        cp = subprocess.run([str(BIN), 'patch', str(src), str(out), '--nor-profile','m6m','--save-type', 'eeprom'],
                            text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        assert cp.returncode != 0 and 'EEPROM_V111' in cp.stderr

    print('PASS FLASH512 V130/V131/V133 primitive-only adapters + anchored storage')
    print('PASS EEPROM V120/V121/V122/V124/V126 + EEPROM512 primitive-only adapters')
    print('PASS SRAM/SRAM_F stock wrappers + RAM shadow + IRQ hotkey commit')
    print('PASS EEPROM_V111 fail-closed refusal')


if __name__ == '__main__':
    main()
