#!/usr/bin/env python3
"""Create a conservative libgbasave SRAM/FRAM save-memory profile.

This tool writes declarative hardware semantics only. It never marks a profile
hardware-proven and never guesses a bank selector.
"""
from __future__ import annotations

import argparse
import json
import re
from pathlib import Path

KEY_RE = re.compile(r'^[a-z][a-z0-9_]{1,47}$')


def intval(value: str) -> int:
    return int(value, 0)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument('--key', required=True)
    parser.add_argument('--display-name', required=True)
    parser.add_argument('--technology', choices=('SRAM', 'FRAM'), required=True)
    parser.add_argument('--total-bytes', type=intval, required=True)
    parser.add_argument('--window-bytes', type=intval, required=True)
    parser.add_argument('--gba-window-base', type=intval, default=0x0E000000)
    parser.add_argument('--bank-count', type=intval, default=1)
    parser.add_argument('--selector-kind', choices=('NONE', 'ROM_WRITE_DATA_BITS'), default='NONE')
    parser.add_argument('--selector-gba-address', type=intval, default=0)
    parser.add_argument('--selector-mask', type=intval, default=0)
    parser.add_argument('--selector-shift', type=intval, default=0)
    parser.add_argument('--selector-fixed-value', type=intval, default=0)
    parser.add_argument('--selector-write-width', type=intval, choices=(8, 16), default=16)
    parser.add_argument('--enable-eeprom', action='store_true')
    parser.add_argument('--enable-flash512', action='store_true')
    parser.add_argument('--enable-flash1m', action='store_true')
    parser.add_argument('--qualification-note', default='Generated profile; real hardware qualification required.')
    parser.add_argument('--force', action='store_true')
    args = parser.parse_args()

    if not KEY_RE.match(args.key):
        parser.error('key must match [a-z][a-z0-9_]{1,47}')
    if args.total_bytes <= 0 or args.window_bytes <= 0 or args.bank_count <= 0:
        parser.error('sizes and bank count must be positive')
    if args.window_bytes * args.bank_count != args.total_bytes:
        parser.error('window-bytes * bank-count must equal total-bytes')
    if args.bank_count == 1 and args.selector_kind != 'NONE':
        parser.error('an unbanked profile must use selector-kind NONE')
    if args.bank_count > 1 and args.selector_kind == 'NONE':
        parser.error('a banked profile requires an explicit selector')
    if args.selector_kind != 'NONE' and args.selector_gba_address == 0:
        parser.error('selector-gba-address is required for a banked selector')
    if args.enable_flash1m and args.bank_count < 2:
        parser.error('FLASH1M conversion requires a banked target')

    capabilities = [
        'NONVOLATILE',
        'BYTE_RW',
        'SRAM_WINDOW',
        'DIRECT_SRAM_GAMEPLAY',
    ]
    if args.bank_count > 1:
        capabilities.append('BANKED_WINDOW')
    if args.enable_eeprom:
        capabilities.append('EEPROM_TO_RAM')
    if args.enable_flash512:
        capabilities.append('FLASH512_TO_RAM')
    if args.enable_flash1m:
        capabilities.append('FLASH1M_TO_BANKED_RAM')

    selector = {'kind': args.selector_kind}
    if args.selector_kind != 'NONE':
        selector.update({
            'gba_address': args.selector_gba_address,
            'fixed_value': args.selector_fixed_value,
            'mask': args.selector_mask,
            'shift': args.selector_shift,
            'write_width': args.selector_write_width,
        })

    profile = {
        'schema_version': 1,
        'key': args.key,
        'display_name': args.display_name,
        'technology': args.technology,
        'capabilities': capabilities,
        'total_bytes': args.total_bytes,
        'window_bytes': args.window_bytes,
        'gba_window_base': args.gba_window_base,
        'bank_count': args.bank_count,
        'selector': selector,
        'qualification': {
            'state': 'unqualified',
            'note': args.qualification_note,
        },
    }

    out = args.root.resolve() / 'config/save_memory/profiles' / f'{args.key}.json'
    if out.exists() and not args.force:
        parser.error(f'{out} already exists; use --force to replace it')
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(profile, indent=2) + '\n')
    print(out)
    print('Created as UNQUALIFIED. Run gen_save_memory_profiles.py and hardware-qualify before promotion.')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
