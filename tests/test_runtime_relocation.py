#!/usr/bin/env python3
from __future__ import annotations
import pathlib, re, struct, subprocess, tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
from test_support import GBASAVEHANDLER_BIN as BIN
LINK_BASE = 0x08400000


def put(data: bytearray, off: int, hexs: str):
    raw = bytes.fromhex(hexs)
    data[off:off + len(raw)] = raw


def fixture() -> bytearray:
    data = bytearray(0x100000)
    data[0xA0:0xAC] = b'RELOC TEST  '
    data[0xAC:0xB0] = b'RELO'
    data[0x1000:0x1000 + len(b'FLASH512_V131')] = b'FLASH512_V131'
    put(data, 0x1800, '30B591B0684600F0')
    put(data, 0x2000, '10B5041C531E002A')
    put(data, 0x2800, '30B5051C0B1C541E')
    put(data, 0x4000, '70B590B0154D2988')
    put(data, 0x5000, '70B5464640B490B0')
    put(data, 0x6000, 'F0B590B00F1C0004040C034800684089844205D3014840E00000FF800000201CFFF7D7FE0004050C002D35D1')
    # Verified code cave for the runtime. Save storage remains appended tail.
    data[0xE0000:0xF0000] = b'\xff' * 0x10000
    return data


def main():
    with tempfile.TemporaryDirectory() as td:
        td = pathlib.Path(td)
        src, out = td / 'in.gba', td / 'out.gba'
        original = fixture()
        src.write_bytes(original)
        text = subprocess.check_output([str(BIN), 'patch', str(src), str(out), '--nor-profile','m6m','--save-type', 'flash512'], text=True)
        match = re.search(r'Runtime:\s+0x([0-9A-F]+)\.\.0x([0-9A-F]+)\s+(\S+)', text)
        assert match
        off = int(match.group(1), 16)
        assert match.group(3) == 'APPENDED_TAIL'
        assert 'Placement proof: APPENDED_WITHIN_TARGET_CAPACITY' in text

        patched = out.read_bytes()
        assert off >= len(original), 'database-miss runtime must append, never authorize raw FF/00 caves'

        header = (ROOT / 'generated' / 'runtime_blob.h').read_text()
        blobsec = re.search(r'kRuntimeBlob\[\] = \{(.*?)\};', header, re.S)
        assert blobsec
        runtime = bytes(int(x, 16) for x in re.findall(r'0x([0-9A-Fa-f]{2})', blobsec.group(1)))
        relsec = re.search(r'kRuntimeAbsolute32Relocations\[\] = \{([^}]*)\}', header, re.S)
        assert relsec
        relocs = [int(x, 16) for x in re.findall(r'0x([0-9A-Fa-f]+)u', relsec.group(1))]
        delta = (0x08000000 + off) - LINK_BASE
        for relocation in relocs:
            before = struct.unpack_from('<I', runtime, relocation)[0]
            after = struct.unpack_from('<I', patched, off + relocation)[0]
            assert after == ((before + delta) & 0xffffffff), (
                hex(relocation), hex(before), hex(after), hex(delta))

        storage0 = re.search(r'block 0 -> 0x([0-9A-F]+)\.\.0x([0-9A-F]+) APPENDED_TAIL', text)
        assert storage0 and int(storage0.group(1), 16) >= len(original)
        print(f'PASS runtime relocation: {len(relocs)} ABS32 literals shifted by {delta:#x}')
        print('PASS database-miss FF cave is not trusted; runtime/storage append while relocations remain correct')


if __name__ == '__main__':
    main()
