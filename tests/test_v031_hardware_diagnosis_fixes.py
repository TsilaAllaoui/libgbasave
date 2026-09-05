#!/usr/bin/env python3
"""v0.31 hardware-diagnosis regression gates.

These gates encode only generic backend/protocol facts learned from hardware:
* backend dispatch obeys the ARM callee-saved ABI before M6/M36 selection;
* M36 FLASH512 uses the shared Nintendo primitive adapter + M36 E8 RWW writer;
* the already-proven M36 FLASH1M compact route remains selected independently.
No production decision is selected by title or game code.
"""
from pathlib import Path
import re, subprocess, tempfile

ROOT = Path(__file__).resolve().parents[1]
from test_support import GBASAVEHANDLER_BIN as BIN
ROMSET = Path('/mnt/data/romset')
SONIC = ROMSET / 'Sonic.gba'       # exact FLASH512 fixture
LEAFGREEN = ROMSET / 'LeafGreen.gba'  # exact FLASH1M control fixture


def patch(src: Path, out: Path) -> str:
    return subprocess.check_output(
        [str(BIN), 'patch', str(src), str(out), '--nor-profile', 'm36'],
        text=True,
    )


def function_body(source: str, name: str, next_name: str) -> str:
    m = re.search(rf'^{re.escape(name)}:\n(.*?)(?=^{re.escape(next_name)}:)', source, re.M | re.S)
    assert m, name
    return m.group(1)


def main() -> None:
    assert BIN.exists(), BIN
    if not (SONIC.exists() and LEAFGREEN.exists()):
        print('SKIP v031 exact FLASH fixtures unavailable'); return

    asm = (ROOT / 'runtime' / 'sram_hotkey_irq.S').read_text()
    program = function_body(asm, 'gbashRunSramStackProgram', 'gbashRunSramStackErase')
    # The bug observed on M6 hardware was dispatch writing r4 before it was
    # saved. The payload then programmed successfully, but caller verification
    # used clobbered r4 and aborted before SR16 metadata/commit.
    assert program.index('push {r4-r7, lr}') < program.index('movs r4, #0xE8')
    assert 'bl gbashRunM36RwwProgram' in program
    assert program.count('pop {r4-r7, pc}') >= 2

    erase_tail = asm[asm.index('gbashRunSramStackErase:'):]
    assert erase_tail.index('push {r4-r5, lr}') < erase_tail.index('movs r4, #0xE8')
    assert 'bl gbashRunM36RwwErase' in erase_tail
    assert erase_tail.count('pop {r4-r5, pc}') >= 2
    print('PASS v031 backend dispatch: callee-saved registers preserved before M6/M36 selection')

    runtime_src = (ROOT / 'runtime' / 'gba_runtime.cpp').read_text()
    bank_fn = re.search(r'static inline void selectBank\(u32 bank\)\n\{(.*?)\n\}', runtime_src, re.S)
    assert bank_fn
    assert bank_fn.group(1).index('if (gRuntimeConfig.bankCount <= 1u)') < bank_fn.group(1).index('reg8(gRuntimeConfig.currentBankStateAddress)')
    print('PASS v031 FLASH512 RAM ownership: single-bank route performs no bank-state EWRAM write')

    with tempfile.TemporaryDirectory() as td:
        td = Path(td)
        sonic = td / 'flash512.gba'
        ts = patch(SONIC, sonic)
        assert 'FLASH512 / 65536 bytes' in ts
        assert 'Storage mode:  FLASH_VERSIONED_SLOTS' in ts
        assert 'FLASH versions/block:  15' in ts
        assert 'FLASH spill blocks:    8' in ts
        assert 'M36 RWW runtime bank:' in ts
        assert 'M36 R13G FLASH compact dispatcher' not in ts
        # 16 primary 4 KiB logical sectors + 8 spill units, each represented by
        # one independent 64 KiB program-only physical storage unit.
        blocks = re.findall(r'block\s+\d+\s+->\s+0x([0-9A-F]+)\.\.0x([0-9A-F]+)', ts)
        assert len(blocks) == 24, len(blocks)
        runtime = re.search(r'Runtime:\s+0x([0-9A-F]+)\.\.0x([0-9A-F]+)', ts)
        assert runtime
        rlo, rhi = (int(x, 16) for x in runtime.groups())
        rbank = rlo >> 20
        assert (rhi >> 20) == rbank
        for a, b in blocks:
            a, b = int(a, 16), int(b, 16)
            assert b - a + 1 == 0x10000
            assert (a >> 20) != rbank and (b >> 20) != rbank
            assert b < 0xFE0000
        print('PASS v031 M36 FLASH512: generic versioned-slot frontend + separated E8 RWW backend')

        leaf = td / 'flash1m.gba'
        tl = patch(LEAFGREEN, leaf)
        assert 'FLASH1M / 131072 bytes' in tl
        # Compact FLASH1M route reports one dispatcher routine and six
        # 128 KiB physical blocks; the generic FLASH512 route reports six
        # primitive bridges and 24 x 64 KiB program-only units.
        assert 'Patched routines: 1' in tl
        leaf_blocks = re.findall(r'block\s+\d+\s+->\s+0x([0-9A-F]+)\.\.0x([0-9A-F]+)', tl)
        assert len(leaf_blocks) == 6
        assert all(int(b,16)-int(a,16)+1 == 0x20000 for a,b in leaf_blocks)
        print('PASS v031 M36 FLASH1M control: proven compact route retained')

        sonic2 = td / 'flash512_2.gba'
        patch(SONIC, sonic2)
        assert sonic.read_bytes() == sonic2.read_bytes()
        print('PASS v031 FLASH512 deterministic generation')


if __name__ == '__main__':
    main()
