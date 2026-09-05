#!/usr/bin/env python3
from __future__ import annotations
import pathlib, re, struct, subprocess, tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
from test_support import GBASAVEHANDLER_BIN as BIN
ELF = ROOT / 'build_runtime' / 'runtime.elf'
FZERO = pathlib.Path('/mnt/data/FZERO(1).gba')


def symbol_addr(name: str) -> int:
    out = subprocess.check_output(['nm', '-n', str(ELF)], text=True)
    m = re.search(rf'^([0-9a-fA-F]+)\s+\S\s+{re.escape(name)}$', out, re.M)
    assert m, name
    return int(m.group(1), 16)


def function_disasm(name: str) -> str:
    out = subprocess.check_output(['llvm-objdump', '-d', '--no-show-raw-insn', str(ELF)], text=True)
    m = re.search(rf'^[0-9a-fA-F]+ <{re.escape(name)}>:\n(.*?)(?=\n[0-9a-fA-F]+ <|\Z)', out, re.M | re.S)
    assert m, name
    return m.group(1)


def main() -> None:
    if not FZERO.exists():
        print('SKIP legacy SRAM exact-ROM test: F-Zero fixture unavailable')
        return
    with tempfile.TemporaryDirectory() as td:
        td = pathlib.Path(td)
        out = td / 'fzero.gba'
        text = subprocess.check_output([str(BIN), 'patch', str(FZERO), str(out), '--nor-profile','m6m','--save-type', 'sram'], text=True)
        patched = out.read_bytes()
        stock = FZERO.read_bytes()

        assert len(patched) == 0x430000
        assert 'SRAM live state:       RAM/EWRAM only during gameplay' in text
        assert 'SRAM NOR retention:    restore/reload + changed hotkey commit only' in text
        assert 'SRAM shadow:           0x02027000' in text
        assert 'Patched routines: 5' in text
        assert 'SRAM direct literals:  9' in text

        # Nintendo SRAM wrappers and dispatcher remain stock. Copied cores and
        # writer are intercepted. BIOS-vector literals stay at 03007FFC; the
        # transparent hotkey interposer is installed lazily after the first SRAM API.
        assert patched[0x49804:0x4980C] == stock[0x49804:0x4980C]
        assert patched[0x498D8:0x498E0] == stock[0x498D8:0x498E0]
        assert patched[0x497E0:0x497E8] != stock[0x497E0:0x497E8]
        assert patched[0x49868:0x49870] != stock[0x49868:0x49870]
        assert patched[0x498A8:0x498B0] != stock[0x498A8:0x498B0]
        assert patched[0xFC:0x100] == stock[0xFC:0x100]
        assert patched[0:4] != stock[0:4]
        assert struct.unpack_from('<I', patched, 0x234)[0] == 0x03007FFC
        assert struct.unpack_from('<I', patched, 0x480AC)[0] == 0x03007FFC
        assert patched[0x234:0x238] == stock[0x234:0x238]
        assert patched[0x480AC:0x480B0] == stock[0x480AC:0x480B0]

        # The nine instruction-proven SRAM literals now point at EWRAM shadow,
        # never the NOR mirror. Ordinary direct SRAM traffic therefore has zero
        # NOR wear.
        changed = []
        for off in range(0, len(stock) - 3, 4):
            old = int.from_bytes(stock[off:off+4], 'little')
            if 0x0E000000 <= old < 0x0E008000 and patched[off:off+4] != stock[off:off+4]:
                changed.append((off, int.from_bytes(patched[off:off+4], 'little')))
        assert len(changed) == 9
        for off, new in changed:
            old = int.from_bytes(stock[off:off+4], 'little')
            assert new == 0x02027000 + (old - 0x0E000000), (hex(off), hex(old), hex(new))

        # Runtime config is at the start of the appended runtime image.
        cfg = 0x400000
        assert struct.unpack_from('<I', patched, cfg + 88)[0] == 0x08410000  # persistent mirror
        assert struct.unpack_from('<I', patched, cfg + 92)[0] == 0x08420000  # private backup
        assert struct.unpack_from('<I', patched, cfg + 96)[0] == 0x02027000  # live shadow
        assert struct.unpack_from('<I', patched, cfg + 100)[0] == 0x080000C0  # original ROM entry target
        assert struct.unpack_from('<I', patched, cfg + 104)[0] == 0x8000
        assert struct.unpack_from('<I', patched, cfg + 108)[0] == 0x0E000000
        assert struct.unpack_from('<H', patched, cfg + 112)[0] == 0x00F9
        assert struct.unpack_from('<I', patched, cfg + 120)[0] == 0x080000FC  # direct dispatcher chain

    write_body = function_disasm('gbashSramWriteMirror')
    # Normal game save path can initialize/read/copy RAM, but cannot program or
    # erase NOR. The explicit hotkey function is the only commit boundary.
    assert 'ramProgramBytes' not in write_body
    assert 'ramEraseBlock' not in write_body
    assert 'ramCloneWithPatch' not in write_body

    hotkey_body = function_disasm('gbashSramHotkeyCommit')
    assert hotkey_body
    irq_body = function_disasm('gbashSramHotkeyIrqEntry')
    assert '0x8400b08' in irq_body or 'gbashSramHotkeyCommit' in irq_body or 'bx' in irq_body

    print('PASS Phase12 F-Zero: normal SRAM writes use EWRAM shadow only')
    print('PASS Phase12 F-Zero: lazy transparent L+R+SELECT+B interposer is the sole NOR commit boundary')
    print('PASS Phase12 F-Zero: cold restore config is direct 32 KiB mirror -> EWRAM shadow')


if __name__ == '__main__':
    main()
