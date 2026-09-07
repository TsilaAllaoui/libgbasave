#!/usr/bin/env python3
"""Generic direct-RWW planning/composition must not select behavior by chip name."""
from pathlib import Path
import json

ROOT = Path(__file__).resolve().parents[1]


def main() -> None:
    generic_paths = [
        ROOT / 'src/direct_protocol_patcher.cpp',
        ROOT / 'src/embedded/nor_save_layout.cpp',
        ROOT / 'src/embedded/nor_patch.cpp',
        ROOT / 'include/gbasave/direct_protocol_patcher.h',
        ROOT / 'include/gbasave/embedded/nor_save_layout.h',
        ROOT / 'include/gbasave/embedded/nor_patch.h',
    ]
    generic_text = '\n'.join(p.read_text(errors='ignore') for p in generic_paths).lower()
    for forbidden in ('m36', 'nor_driver_m36'):
        assert forbidden not in generic_text, f'generic planner/composer leaks chip identity: {forbidden}'

    protocol = json.loads((ROOT / 'config/nor/protocols/intel-e8-rww-128k.json').read_text())
    chip = json.loads((ROOT / 'config/nor/chips/m36.json').read_text())
    assert protocol['key'] == 'intel-e8-rww-128k'
    assert protocol['driver_enum'] == 'IntelE8BufferedRww'
    assert protocol['direct_patch_engine'] == 'rww-compact-v1'
    assert chip['protocol_ref'] == protocol['key']

    save_types = (ROOT / 'include/gbasave/save_types.h').read_text()
    assert 'IntelE8BufferedRww = 2' in save_types
    assert 'IntelStatusRegisterWord10 = IntelE8BufferedRww' in save_types

    # Frozen historical runtime/asset source may retain old symbol/marker names
    # because changing those sources or payload bytes would weaken hardware
    # rollback guarantees.  They must not participate in host route selection.
    print('PASS direct-RWW target neutrality: generic planner/composer is chip-name free; M36 is only a target profile selecting a reusable protocol')


if __name__ == '__main__':
    main()
