#!/usr/bin/env python3
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
from test_support import GBASAVEHANDLER_BIN as BIN
SRC = (ROOT / 'src' / 'save_library.cpp').read_text()
FZERO = Path('/mnt/data/FZERO(1).gba')
MMZ = Path('/mnt/data/roms_new/Mega Man Zero (USA, Europe).gba')


def scan(path: Path) -> str:
    return subprocess.check_output([str(BIN), 'scan', str(path), '--save-type', 'sram'], text=True)


def main() -> None:
    body = SRC.split('bool privateSramShadowIsCompatible(const RomImage &rom)', 1)[1].split('\n}\n\nSramRamMirrorEvidence', 1)[0]
    # Raw aligned pointer-looking words are not authoritative RAM references.
    assert 'for (std::size_t offset = 0u; offset + 4u <= rom.size(); offset += 4u)' not in body
    assert 'hasStrongThumbPrologue' in body
    assert 'hasStrongArmPrologue' in body
    assert 'push {...,lr}' in body

    if FZERO.exists():
        text = scan(FZERO)
        assert 'SRAM legacy private shadow: RAM_AUDIT_PASS' in text
        print('PASS SRAM private workspace: exact hardware-proven F-Zero ignores weak/filler fake Thumb references')
    else:
        print('SKIP SRAM private workspace exact F-Zero fixture unavailable')

    if MMZ.exists():
        text = scan(MMZ)
        assert 'SRAM legacy private shadow: RAM_AUDIT_REJECT' in text
        print('PASS SRAM private workspace: exact MMZ still rejects strong function-backed private-RAM conflicts')
    else:
        print('SKIP SRAM private workspace exact MMZ fixture unavailable')


if __name__ == '__main__':
    main()
