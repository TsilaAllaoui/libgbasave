#!/usr/bin/env python3
from pathlib import Path
import hashlib, os, subprocess, tempfile

ROOT = Path(__file__).resolve().parents[1]
from test_support import GBASAVEHANDLER_BIN as BIN
FIX = Path(os.environ.get('GBASH_FIXTURE_DIR','/mnt/data/roms_work'))
CASES = [
    ('Advance Wars 2 - Black Hole Rising (U).gba', 'Nintendo FLASH_V126', 'FLASH512', 0x800000),
    ('Sonic Advance (USA) (En,Ja).gba', 'Nintendo FLASH_V126', 'FLASH512', 0x980000),
    ('Pokemon - LeafGreen Version (USA).gba', 'Nintendo FLASH1M_V103', 'FLASH1M', 0x1000000),
]
missing=[name for name,_,_,_ in CASES if not (FIX/name).exists()]
if missing:
    print('SKIP exact FLASH compatibility/size regression: fixtures unavailable:', ', '.join(missing))
    raise SystemExit(0)

def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()

with tempfile.TemporaryDirectory() as td:
    td=Path(td)
    for name, library, save_type, expected_size in CASES:
        src=FIX/name
        outs=[]; reports=[]
        for n in (1,2):
            out=td/f'{n}_{name}'
            cp=subprocess.run([str(BIN),'patch',str(src),str(out),'--nor-profile','m6m'],check=True,text=True,stdout=subprocess.PIPE)
            outs.append(out); reports.append(cp.stdout)
        assert outs[0].read_bytes()==outs[1].read_bytes(), name+' non-deterministic output'
        report=reports[0]
        assert f'Save library:  {library}' in report, (name, report)
        assert f'Save type:     {save_type} /' in report, (name, report)
        assert outs[0].stat().st_size == expected_size, (name, hex(outs[0].stat().st_size), hex(expected_size))
        assert f'Output size:   0x{expected_size:X}' in report
        assert 'GBABR_v6 MATCH' in report
        if expected_size in (0x800000,0x1000000):
            assert 'INTERNAL_FF_HOLE via GBABR_DB' in report
        if name.startswith('Sonic'):
            # Exact plan has only a ~28 KiB SAFE FF tail, no full 64 KiB erase
            # unit. Refuse to fake an 8 MiB image; tail growth is intentional.
            assert 'APPENDED_TAIL' in report
        print(name, sha(outs[0]), hex(expected_size))
print('PASS exact FLASH_V126/FLASH1M_V103 recognition + deterministic compact sizing')
