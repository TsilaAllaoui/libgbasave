#!/usr/bin/env python3
from __future__ import annotations
import pathlib, re, subprocess, tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
from test_support import GBASAVEHANDLER_BIN as BIN
ELF = ROOT / 'build_runtime' / 'runtime.elf'
RUNTIME_SRC = (ROOT / 'runtime' / 'gba_runtime.cpp').read_text()
IRQ_SRC = (ROOT / 'runtime' / 'sram_hotkey_irq.S').read_text()
FZERO = pathlib.Path('/mnt/data/FZERO(1).gba')


def body(name: str) -> str:
    m = re.search(rf'u32\s+{re.escape(name)}\([^)]*\)\s*\{{(.*?)\n\}}', RUNTIME_SRC, re.S)
    assert m, name
    return m.group(1)


def main() -> None:
    if not FZERO.exists():
        print('SKIP legacy SRAM exact-ROM test: F-Zero fixture unavailable')
        return

    boot = body('gbashSramBootInit')
    read = body('gbashSramReadMirror')
    write = body('gbashSramWriteMirror')
    verify = body('gbashSramVerifyMirror')
    hotkey = body('gbashSramHotkeyCommit')

    # Regression for the real-hardware persistence bug: never restore the
    # snapshot at reset, because Nintendo crt0 clears EWRAM afterward.
    assert 'copyFromVolatile' not in boot
    assert 'state.initialized = 0u' in boot
    assert 'ensureSramShadowInitialized();' in read
    assert 'ensureSramShadowInitialized();' in write
    assert 'ensureSramShadowInitialized();' in verify
    assert 'ensureSramShadowInitialized();' in hotkey

    # Hotkey latch is private EWRAM, not BIOS soft-reset bookkeeping.
    assert 'SRAM_HOTKEY_LATCH,       0x0203BFE8' in IRQ_SRC
    assert '0x03007FFB' not in IRQ_SRC

    # The initializer is a plain ROM-array -> EWRAM copy and must not call the
    # NOR command worker.
    init_match = re.search(r'static void ensureSramShadowInitialized\(\)\s*\{(.*?)\n\}', RUNTIME_SRC, re.S)
    assert init_match
    init = init_match.group(1)
    assert 'copyFromVolatile' in init
    assert 'programBytesWorker' not in init
    assert 'eraseBlockWorker' not in init
    assert 'cloneWithPatchWorker' not in init

    with tempfile.TemporaryDirectory() as td:
        out = pathlib.Path(td) / 'fzero.gba'
        subprocess.check_call([str(BIN), 'patch', str(FZERO), str(out), '--nor-profile','m6m','--save-type', 'sram'], stdout=subprocess.DEVNULL)
        patched = out.read_bytes()
        assert len(patched) == 0x430000

    print('PASS Phase13 SRAM: reset hook installs IRQ only; no early EWRAM restore')
    print('PASS Phase13 SRAM: first Read/Write/Verify lazily restores committed mirror')
    print('PASS Phase13 SRAM: hotkey latch moved off BIOS 0x03007FFB')


if __name__ == '__main__':
    main()
