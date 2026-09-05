#!/usr/bin/env python3
"""Hardware-backed NOR profile gates: M6 word40 and M36 RWW/E8."""
from pathlib import Path
import hashlib,re,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[1]
from test_support import GBASAVEHANDLER_BIN as BIN
RUNTIME=(ROOT/'runtime/gba_runtime.cpp').read_text()
IRQ=(ROOT/'runtime/sram_hotkey_irq.S').read_text()
BACKENDS=(ROOT/'generated/nor_profile_db.h').read_text()
PATCHER=(ROOT/'src/save_patcher.cpp').read_text()
LAYOUT=(ROOT/'src/storage_layout.cpp').read_text()
def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def patch(src,profile,out): return subprocess.check_output([str(BIN),'patch',str(src),str(out),'--nor-profile',profile],text=True)

def static_backend_audit():
    assert '0x0040u' in BACKENDS and '0x00E8u' in BACKENDS
    assert '65536u' in BACKENDS and '131072u' in BACKENDS
    # Proven M36 save-runtime transaction, not speculative 0x10 word program.
    for token in ('0x0050u','0x0060u','0x00D0u','0x00E8u','0x003Au','0x000Bu','0x000Fu'):
        assert token in RUNTIME, token
    assert '*target = 15u' in RUNTIME
    assert 'ramM36UnlockBlock' in RUNTIME and 'ramM36ProgramLine32' in RUNTIME
    assert 'target[index] != words[index]' in RUNTIME
    # Known-bad FIX162B-style whole-worker-below-SP relocation is forbidden.
    assert '.Lrun_m36_program_worker' not in IRQ
    assert '.Lrun_m36_erase_worker' not in IRQ
    assert 'gbashRunM36RwwProgram' in IRQ and 'gbashRunM36RwwErase' in IRQ
    assert 'usesM36E8Backend()' in RUNTIME
    assert 'return &ramProgramBytes;' in RUNTIME and 'return &ramEraseBlock;' in RUNTIME
    assert 'ramM36WorkerBankSeparated' in RUNTIME
    # Host derives RWW-bank and mutable-array geometry from the backend descriptor,
    # then reserves the complete execution bank from generic storage.
    assert '1048576u' in BACKENDS and '16646144u' in BACKENDS
    assert 'backend.runtimeExecutionBankBytes' in PATCHER
    assert 'reservedRanges.push_back({bankStart, backend.runtimeExecutionBankBytes})' in PATCHER
    assert 'backend.mutableMainArrayEnd' in PATCHER
    assert 'executionBankBytes' in LAYOUT
    # FLASH/EEPROM packing stays compact; only erase-capable SRAM uses physical erase geometry.
    assert 'options.programStorageBlockBytes' in PATCHER
    assert 'backend.eraseBlockBytes' in PATCHER
    print('PASS NOR architecture: M6 word40 preserved; M36 unlock+E8+WAITCNT+verify direct-RWW backend; no whole-worker stack relocation')

def assert_m36_rww_layout(text,name):
    rm=re.search(r'M36 RWW runtime bank:\s+(\d+)',text); assert rm,name
    rb=int(rm.group(1))
    rr=re.search(r'Runtime:\s+0x([0-9A-F]+)\.\.0x([0-9A-F]+)',text); assert rr
    rlo,rhi=[int(x,16) for x in rr.groups()]
    assert rlo>>20 == rhi>>20 == rb,(name,hex(rlo),hex(rhi),rb)
    found=False
    for lo,hi in re.findall(r'(?:block \d+ ->|journal span \d+ ->) 0x([0-9A-F]+)\.\.0x([0-9A-F]+)',text):
        found=True; lo=int(lo,16); hi=int(hi,16)
        assert lo>>20 != rb and hi>>20 != rb,(name,'storage shares runtime RWW bank',rb,hex(lo),hex(hi))
    assert found,name
    return rb

def exact_fixture_audit():
    fr=Path('/mnt/data/romset')
    aw2=fr/'Advance Wars 2 - Black Hole Rising (U).gba'
    m36=[
      ('Astro',fr/'Astro Boy - Omega Factor (Europe) (En,Ja,Fr,De,Es,It).gba'),
      ('Kirby',fr/'Kirby - Nightmare in Dream Land (USA).gba'),
      ('MetalSlug',fr/'Metal Slug Advance (USA).gba'),
      ('LeafGreen',fr/'Pokemon - LeafGreen Version (USA).gba'),
      ('Sonic',fr/'Sonic Advance (USA) (En,Ja).gba')]
    if not aw2.exists() or not all(p.exists() for _,p in m36): print('SKIP exact NOR-profile fixtures unavailable'); return
    with tempfile.TemporaryDirectory() as td:
      td=Path(td); a=td/'aw2a.gba'; b=td/'aw2b.gba'
      ta=patch(aw2,'m6m',a); patch(aw2,'intel-word40',b)
      assert a.read_bytes()==b.read_bytes() and len(a.read_bytes())==0x800000
      assert 'NOR program cmd: 0x40' in ta
      print(f'PASS M6/M6M exact AW2: 8 MiB word40 deterministic sha256={sha(a)}')
      for name,src in m36:
        a=td/f'{name}a.gba'; b=td/f'{name}b.gba'
        ta=patch(src,'m36',a); tb=patch(src,'m36-e8-128k',b)
        assert a.read_bytes()==b.read_bytes(),name
        assert len(a.read_bytes())<=0x1000000,(name,hex(len(a.read_bytes())))
        assert 'NOR program cmd: 0xE8' in ta and 'NOR erase unit:  131072 bytes' in ta
        rb=assert_m36_rww_layout(ta,name)
        rr=re.search(r'Runtime:\s+0x([0-9A-F]+)\.\.0x([0-9A-F]+)',ta); assert rr
        rlo=int(rr.group(1),16); assert (rlo & ~0xFFFFF)+0x100000 <= 0xFE0000,(name,hex(rlo))
        for lo,hi in re.findall(r'block \d+ -> 0x([0-9A-F]+)\.\.0x([0-9A-F]+)',ta):
          assert int(hi,16) < 0xFE0000,(name,lo,hi)
        if name=='Kirby':
          blocks=re.findall(r'block \d+ -> 0x([0-9A-F]+)\.\.0x([0-9A-F]+)',ta); assert len(blocks)==2
          for lo,hi in blocks:
            lo=int(lo,16);hi=int(hi,16);assert lo%0x20000==0 and hi-lo+1==0x20000
        print(f'PASS M36 exact {name}: RWW bank {rb}, E8 deterministic, size={len(a.read_bytes()):#x}, sha256={sha(a)}')
if __name__=='__main__':
    assert BIN.exists(); static_backend_audit(); exact_fixture_audit()
