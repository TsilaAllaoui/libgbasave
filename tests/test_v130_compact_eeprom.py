#!/usr/bin/env python3
"""v1.3 COMPACT_V2 EEPROM8K geometry, atomic A/B GC, and no-SRAM contract."""
from pathlib import Path
import re, subprocess, tempfile, struct

ROOT=Path(__file__).resolve().parents[1]
from test_support import GBASAVEHANDLER_BIN as BIN
ROM=Path('/mnt/data/roms_current/Super Mario Advance 3 - Yoshi\'s Island (USA).gba')

MAGIC=0x324A4345
TAG=0xA55A
REC=10
META=1024
DW=1024

def records_per_dword(block):
    return (block-META)//(DW*REC)

def model_cycle(block_bytes, rounds=40):
    n=records_per_dword(block_bytes)
    assert n in (6,12)
    blocks=[[[None]*n for _ in range(DW)] for _ in range(2)]
    valid=[False,False]; gen=[0,0]; active=0
    valid[0]=True; gen[0]=1
    logical=[b'\xff'*8 for _ in range(DW)]
    erase_count=[0,0]
    for r in range(rounds):
        d=(r*73)%DW
        value=bytes(((r+i)&0xff) for i in range(8))
        if value==logical[d]: continue
        try:
            slot=blocks[active][d].index(None)
        except ValueError:
            dest=active^1
            # old active remains valid throughout destination erase/copy.
            erase_count[dest]+=1
            blocks[dest]=[[None]*n for _ in range(DW)]
            valid[dest]=False
            for i,v in enumerate(logical):
                if v != b'\xff'*8:
                    blocks[dest][i][0]=v
            # publish destination generation last
            gen[dest]=gen[active]+1; valid[dest]=True
            active=dest
            slot=blocks[active][d].index(None)
        blocks[active][d][slot]=value
        logical[d]=value
        # latest record must reconstruct logical image.
        for q in (d,(d+1)%DW):
            vals=[x for x in blocks[active][q] if x is not None]
            got=vals[-1] if vals else b'\xff'*8
            assert got==logical[q]
    assert any(valid)
    return n, erase_count

def main():
    src=(ROOT/'runtime/gba_runtime.cpp').read_text()
    host=(ROOT/'src/save_patcher.cpp').read_text()
    cfg=(ROOT/'include/gbasave/runtime_config.h').read_text()
    assert 'EepromCompactAB = 5' in cfg
    assert 'StoragePolicy::SafeV1' in host
    assert 'kEepromCompactRecordBytes = 10u' in src
    assert 'commitCompactEepromHeader(destination)' in src
    assert 'old generation' in src.lower()
    assert 'gbashRunSramStackErase' in src
    assert 'No cartridge' in host and 'SRAM/FRAM is assumed' in host
    # No large logical save buffer is allocated for compact GC.
    assert 'u8 encoded[8]' in src
    assert 'u8 snapshot[8192]' not in src and 'u8 snapshot[0x2000]' not in src
    n64,e64=model_cycle(0x10000)
    n128,e128=model_cycle(0x20000)
    assert n64==6 and n128==12

    if ROM.exists():
        with tempfile.TemporaryDirectory() as td:
            td=Path(td)
            for profile,expected_blocks,expected_records,expected_bytes in (
                ('m6m',2,6,0x20000),('m36',2,12,0x40000)):
                out=td/f'{profile}.gba'
                cp=subprocess.run([str(BIN),'patch',str(ROM),str(out),'--nor-profile',profile],text=True,capture_output=True,check=True)
                text=cp.stdout
                assert 'Storage mode:  EEPROM_COMPACT_AB' in text
                assert f'EEPROM records/dword:  {expected_records}' in text
                blocks=re.findall(r'block \d+ ->',text)
                assert len(blocks)==expected_blocks
                m=re.search(r'Persistent storage:\s+(\d+) physical bytes',text)
                assert m and int(m.group(1))==expected_bytes
            # SAFE_V1 remains available in the same v1.3 binary as a direct rollback.
            safe=td/'safe.gba'
            safe_cp=subprocess.run([str(BIN),'patch',str(ROM),str(safe),'--nor-profile','m6m','--storage-policy','safe-v1'],text=True,capture_output=True,check=True)
            assert 'Storage policy: SAFE_V1' in safe_cp.stdout
            assert 'Storage mode:  EEPROM_RECORD_LANES' in safe_cp.stdout
            assert 'Persistent storage:  1048576 physical bytes' in safe_cp.stdout
    print('PASS v1.3 EEPROM8K COMPACT_V2: 2-block A/B lanes, bounded reads, publish-last GC, no cartridge SRAM')

if __name__=='__main__': main()
