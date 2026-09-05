#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
production = []
for folder in ('src', 'include', 'runtime'):
    for path in (ROOT / folder).rglob('*'):
        if path.suffix in {'.cpp', '.h', '.S', '.ld'}:
            production.append(path)

text = '\n'.join(path.read_text(errors='ignore') for path in production)
for forbidden in (
    'BMXE', 'AWAE', 'MZM', 'WL4', 'F-Zero', 'Metroid Zero Mission', 'Wario Land 4',
    'BXKP', 'BW5P', 'BZPE', 'BLFE', 'A2CP', 'ACHP', 'BGWJ', 'AQMP', 'BMQP', 'A3MP', 'A3AE', 'AX4E',
    'Castlevania', 'Sonic Advance', 'Dr. Mario', 'Dragon Ball Z', 'Game Boy Wars',
    'Magical Quest', 'Super Mario Advance'
):
    assert forbidden not in text, f'production source contains title-specific token: {forbidden}'

embedded = (ROOT / 'generated' / 'gbabr_region_db.h').read_text()
for forbidden in ('BMXE', 'AWAE', 'AMTE', 'MZM.gba', 'WL4.gba', 'Metroid Fusion'):
    assert forbidden not in embedded, f'embedded runtime lookup contains identity string: {forbidden}'

print('PASS production logic contains no target-title/game-code hardcodes')
