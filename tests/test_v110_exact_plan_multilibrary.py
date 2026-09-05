#!/usr/bin/env python3
"""v1.1 generic exact-plan multi-library and backend-selection gates."""
from pathlib import Path
import hashlib, subprocess, tempfile

ROOT=Path(__file__).resolve().parents[1]
from test_support import GBASAVEHANDLER_BIN as BIN
ROMDIR=Path('/mnt/data/roms_current')
CASES=[
 ('DrMario','2 Games in One! - Dr. Mario + Puzzle League (USA).gba','EEPROM512','m36',6),
 ('DBZ','2-in-1 - Dragon Ball Z Gamepack - The Legacy of Goku I & II (U).gba','EEPROM8K',None,4),
 ('MQ1','Magical Quest Starring Mickey & Minnie (Europe) (En,Fr,De,Es,It).gba','EEPROM512','m36',10),
 ('GBWars','Game Boy Wars Advance 1+2 (Japan).gba','FLASH1M',None,21),
 ('Sonic2in1','2 Games in 1 - Sonic Advance & Sonic Pinball Party (E) (M5).gba','FLASH512',None,12),
]

def main():
 assert BIN.exists()
 scanner=(ROOT/'src/save_library.cpp').read_text()
 exact=(ROOT/'src/exact_plan_save_library.cpp').read_text()
 assert 'scanSaveLibraryFromExactPlan' in scanner
 assert 'scanEeprom' in exact and 'scanFlash512' in exact and 'scanFlash1M' in exact
 assert 'findGbabrExactPlan' in scanner
 production=scanner+exact
 assert 'gameCode() ==' not in production and 'title() ==' not in production
 # Explicit profile is required so an M6 image cannot silently be used on M36.
 sample=next((ROMDIR/n for _,n,_,_,_ in CASES if (ROMDIR/n).exists()),None)
 if sample:
  cp=subprocess.run([str(BIN),'patch',str(sample),'/tmp/v110_should_not_exist.gba'],text=True,capture_output=True)
  assert cp.returncode!=0 and 'explicit --nor-profile' in cp.stderr
 for label,name,stype,hwprofile,hooks in CASES:
  rom=ROMDIR/name
  if not rom.exists():
   print('SKIP',label,'fixture unavailable'); continue
  scan=subprocess.check_output([str(BIN),'scan',str(rom)],text=True)
  assert f'Save type: {stype}' in scan
  # Exact-plan authorization disappears on any ROM mutation; duplicate layouts
  # must then fail closed rather than applying stale offsets.
  if 'multi-library' in scan:
   with tempfile.TemporaryDirectory() as md:
    mutated=Path(md)/'mutated.gba'; data=bytearray(rom.read_bytes()); data[0xC0] ^= 1; mutated.write_bytes(data)
    bad=subprocess.run([str(BIN),'scan',str(mutated)],text=True,capture_output=True)
    assert bad.returncode!=0, label+' mutated exact-plan ROM unexpectedly accepted'
  assert 'multiple ' not in scan.lower()
  if label in ('DrMario','DBZ','MQ1','GBWars','Sonic2in1'):
   assert 'exact-plan multi-library' in scan
  # Count plan-backed primitive map lines (FLASH1M has 7/copy, FLASH512 6/copy).
  primitive_lines=[l for l in scan.splitlines() if l.startswith('  ') and ' entry=' in l]
  assert len(primitive_lines)==hooks, (label,len(primitive_lines),hooks)
  if hwprofile:
   with tempfile.TemporaryDirectory() as td:
    out=Path(td)/(label+'.gba')
    rep=subprocess.check_output([str(BIN),'patch',str(rom),str(out),'--nor-profile',hwprofile],text=True)
    assert 'TARGET CART/NOR PROFILE:' in rep
    assert out.exists() and out.stat().st_size>rom.stat().st_size
 print('PASS v1.1 exact-plan multi-library discovery/bridging + explicit backend selection')

if __name__=='__main__': main()
