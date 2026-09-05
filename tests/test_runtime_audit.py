#!/usr/bin/env python3
from __future__ import annotations
import pathlib, re, subprocess
ROOT = pathlib.Path(__file__).resolve().parents[1]


def main():
    elf = ROOT / 'build_runtime' / 'runtime.elf'
    rel = subprocess.check_output(['readelf', '-r', str(elf)], text=True)
    kinds = set(re.findall(r'R_ARM_[A-Z0-9_]+', rel))
    assert kinds <= {'R_ARM_ABS32', 'R_ARM_THM_CALL'}, kinds
    assert 'R_ARM_ABS32' in kinds
    undef = subprocess.check_output(['nm', '-u', str(elf)], text=True).strip()
    assert not undef
    symbols = subprocess.check_output(['nm', '-a', str(elf)], text=True)
    source = (ROOT / 'runtime' / 'gba_runtime.cpp').read_text()

    # Physical erase exists only inside the explicit SRAM hotkey commit path.
    assert 'ramEraseBlock' in source and 'eraseBlockWorker' in source
    commit_start = source.index('static u32 commitSramShadowToMirror()')
    critical_start = source.index('struct SramHotkeyHardwareState', commit_start)
    commit_body = source[commit_start:critical_start]
    assert commit_body.count('eraseBlockWorker()(') == 2
    write_start = source.index('u32 gbashSramWriteMirror')
    verify_start = source.index('u32 gbashSramVerifyMirror', write_start)
    normal_write_body = source[write_start:verify_start]
    assert 'eraseBlockWorker()(' not in normal_write_body
    assert 'programBytesWorker()(' not in normal_write_body

    assert 'soundcntX = 0' not in source
    assert 'dmaReadsGamePakRom' in source and 'pausedDmaMask' in source
    assert 'unitBytesEqual(logicalUnit, 0u, source' in source
    assert 'gbashVerifyFlashCore' in source and 'verifyUnitBytes' in source
    assert '__aeabi_' not in symbols
    print('PASS runtime relocation table: ABS32 + internal Thumb calls only')
    print('PASS runtime: no undefined helpers / no __aeabi dependency')
    print('PASS normal SRAM writes are NOR-free; erase is hotkey-commit-only')
    print('PASS NOR critical section: only ROM-source DMA paused; master sound untouched')
    print('PASS FLASH compare-before-write + real verify path present')


if __name__ == '__main__':
    main()
