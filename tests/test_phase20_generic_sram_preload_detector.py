#!/usr/bin/env python3
from pathlib import Path
import subprocess, tempfile

ROOT=Path(__file__).resolve().parents[1]
from test_support import GBASAVEHANDLER_BIN as BIN
SRC=(ROOT/'src'/'save_library.cpp').read_text()
PATCH=(ROOT/'src'/'save_patcher.cpp').read_text()
MZM=Path('/mnt/data/rom_inputs/MZM.gba')
WL4=Path('/mnt/data/rom_inputs/WL4.gba')

# Host activation is structural, not title/game-code/library-version hardcoding.
block=SRC[SRC.index('std::uint32_t inferSramPreloadRefreshTrigger'):SRC.index('SaveLibraryMatch scanSram')]
for forbidden in ['BMXE','AWAE','SRAM_V113','0x02038000']:
    assert forbidden not in block, forbidden
assert 'readWrappers' in block and 'qualifyingRefs' in block and 'tailStart' in block
assert 'saveLibrary.sramPreloadRefreshTriggerOffset' in PATCH
for forbidden in ['BMXE','Nintendo SRAM_V113','isMzmUsSelfTestLayout']:
    assert forbidden not in PATCH, forbidden

if MZM.exists():
    m=subprocess.check_output([str(BIN),'scan',str(MZM),'--save-type','sram'],text=True)
    assert 'SRAM preload-refresh trigger: 0x7F80 STRUCTURAL_PROOF' in m
    # Change identifying header fields while preserving code/data. Detection
    # must remain because it is based on instruction evidence, not identity.
    with tempfile.TemporaryDirectory() as td:
        mutant=bytearray(MZM.read_bytes())
        mutant[0xA0:0xAC]=b'GENERIC_TEST'
        mutant[0xAC:0xB0]=b'ZZZZ'
        q=Path(td)/'mutant.gba'; q.write_bytes(mutant)
        r=subprocess.check_output([str(BIN),'scan',str(q),'--save-type','sram'],text=True)
        assert 'SRAM preload-refresh trigger: 0x7F80 STRUCTURAL_PROOF' in r
    print('PASS Phase20 exact MZM preload trigger remains structural under header identity mutation')
else:
    print('SKIP Phase20 exact MZM preload-detector test: fixture unavailable')

if WL4.exists():
    w=subprocess.check_output([str(BIN),'scan',str(WL4),'--save-type','sram'],text=True)
    assert 'SRAM preload-refresh trigger:' not in w
    print('PASS Phase20 exact WL4 negative: mirror proof alone does not enable forced full-read refresh')
else:
    print('SKIP Phase20 exact WL4 preload-detector test: fixture unavailable')

print('PASS Phase20 SRAM preload trigger: conservative structural detector remains identity-free')
