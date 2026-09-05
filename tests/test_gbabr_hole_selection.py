#!/usr/bin/env python3
from pathlib import Path
import subprocess, tempfile

ROOT=Path(__file__).resolve().parents[1]
from test_support import GBASAVEHANDLER_BIN as BIN
MZM=Path('/mnt/data/rom_inputs/MZM.gba')
WL4=Path('/mnt/data/rom_inputs/WL4.gba')
FUSION=Path('/mnt/data/Metroid Fusion (USA, Australia)(1).gba')
assert BIN.exists()
ran=0
with tempfile.TemporaryDirectory() as td:
    td=Path(td)
    if MZM.exists():
        m=subprocess.check_output([str(BIN),'patch',str(MZM),str(td/'m.gba'),'--nor-profile','m6m','--save-type','sram'],text=True)
        assert 'Hole database: GBABR_v6' in m and '0x770000..0x77FFFF' in m and '0x780000..0x78FFFF' in m
        print('PASS exact MZM shared-GBABR hole selection')
        ran+=1
    else:
        print('SKIP exact MZM shared-GBABR hole selection: fixture unavailable')

    if WL4.exists():
        w=subprocess.check_output([str(BIN),'patch',str(WL4),str(td/'w.gba'),'--nor-profile','m6m','--save-type','sram'],text=True)
        assert 'Hole database: GBABR_v6 MATCH' in w and '0x7A0000..0x7AFFFF' in w and '0x7B0000..0x7BFFFF' in w
        print('PASS exact WL4 shared-GBABR hole selection')
        ran+=1
    else:
        print('SKIP exact WL4 shared-GBABR hole selection: fixture unavailable')

    if FUSION.exists():
        f=subprocess.check_output([str(BIN),'patch',str(FUSION),str(td/'f.gba'),'--nor-profile','m6m','--save-type','sram'],text=True)
        assert 'Hole database: GBABR_v6 MATCH' in f
        assert 'Runtime:       0x7C0000..' in f and 'via GBABR_DB' in f
        assert 'block 0 -> 0x7A0000..0x7AFFFF' in f and 'block 1 -> 0x7D0000..0x7DFFFF' in f
        original=FUSION.read_bytes(); patched=(td/'f.gba').read_bytes()
        assert original[0x7B0000:0x7C0000] == patched[0x7B0000:0x7C0000]
        print('PASS exact Fusion shared-GBABR referenced-block quarantine')
        ran+=1
    else:
        print('SKIP exact Fusion shared-GBABR hole selection: fixture unavailable')

if ran:
    print('PASS shared GBABR safe-region selection + database-only authorization for available exact fixtures')
