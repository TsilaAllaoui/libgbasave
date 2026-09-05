#!/usr/bin/env python3
"""v0.30 generic hardware-class regression gates.

No production behavior in this test is selected by title/game code. Exact ROMs
are fixtures used only to exercise protocol/geometry classes that were observed
on hardware: M6 SRAM, M36 EEPROM512/8K, M36 FLASH512/1M, and M36 SRAM.
"""
from pathlib import Path
import hashlib, re, struct, subprocess, tempfile

ROOT=Path(__file__).resolve().parents[1]
from test_support import GBASAVEHANDLER_BIN as BIN
ROMSET=Path('/mnt/data/romset')
FIX={
 'AW2': ROMSET/'AW2.gba',
 'Astro': ROMSET/'Astro.gba',
 'Kirby': ROMSET/'Kirby.gba',
 'MetalSlug': ROMSET/'MetalSlug.gba',
 'LeafGreen': ROMSET/'LeafGreen.gba',
 'Sonic': ROMSET/'Sonic.gba',
}
V016_WORKER_SHA='6a68539bef8b6501de938992832bbbf63e57aad6818c2c9c20aeeb8ac0d9c409'
PARAM_START=0xFE0000

def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def patch(src,profile,out):
    return subprocess.check_output([str(BIN),'patch',str(src),str(out),'--nor-profile',profile], text=True)
def ranges(text):
    return [(int(a,16),int(b,16)) for a,b in re.findall(r'block \d+ -> 0x([0-9A-F]+)\.\.0x([0-9A-F]+)',text)]
def runtime_range(text):
    m=re.search(r'Runtime:\s+0x([0-9A-F]+)\.\.0x([0-9A-F]+)',text); assert m,text
    return tuple(int(x,16) for x in m.groups())
def assert_m36_main_array(text, allow_small_logical=False):
    lo,hi=runtime_range(text); bank=lo & ~0xFFFFF
    assert bank+0x100000 <= PARAM_START,(hex(lo),hex(hi),hex(bank))
    for a,b in ranges(text):
        assert b < PARAM_START,(hex(a),hex(b))
        assert (a>>20)!=(lo>>20) and (b>>20)!=(lo>>20),(hex(a),hex(b),'RWW bank overlap')
        if not allow_small_logical:
            assert a%0x20000==0 and b-a+1==0x20000,(hex(a),hex(b))

def compact_sector_count(rom: bytes, report: str):
    lo,_=runtime_range(report)
    # Compact payload config offset 0x24 is a u32 logical-sector count.
    return struct.unpack_from('<I',rom,lo+0x24)[0]

def main():
    assert BIN.exists(),BIN
    missing=[str(x) for x in FIX.values() if not x.exists()]
    if missing:
        print('SKIP v030 exact hardware-class fixtures unavailable:', ', '.join(missing)); return

    worker=ROOT/'runtime'/'sram_stack_worker.S'
    assert sha(worker)==V016_WORKER_SHA,sha(worker)
    ws=worker.read_text()
    assert 'movs r3, #0x40' in ws
    er=re.search(r'sramStackEraseBlock:(.*?)(?=sramStackEraseBlockEnd:)',ws,re.S); assert er
    assert 'push {r4-r7, lr}' in er.group(1) and 'pop {r4-r7, pc}' in er.group(1)
    assert 'pop {r3-r7, pc}' not in er.group(1)
    print('PASS v030 M6/M6M: exact v0.16 program+erase primitive; balanced erase stack')

    runtime=(ROOT/'runtime'/'gba_runtime.cpp').read_text()
    ep=re.search(r'u32 gbashProgramEepromDword.*?(?=// ---------- Nintendo SRAM)',runtime,re.S); assert ep
    assert ep.group(0).index('latestEepromRecord') < ep.group(0).index('virginEepromRecord')
    assert 'if (identical)' in ep.group(0)
    print('PASS v030 EEPROM wear: compare-before-write precedes append allocation')

    with tempfile.TemporaryDirectory() as td:
        td=Path(td)
        # Frozen hardware controls are checked for protocol identity only; their
        # ROM binaries are not packaged in v0.30.
        aw=td/'aw.gba'; taw=patch(FIX['AW2'],'m6m',aw)
        assert 'NOR program cmd: 0x40' in taw
        assert len(aw.read_bytes())==0x800000
        print('PASS v030 M6 control: AW2 still word40 / 8MiB')

        km=td/'kirby.gba'; tkm=patch(FIX['Kirby'],'m36',km)
        assert 'SRAM / 32768 bytes' in tkm and 'PRIVATE_COMPATIBILITY' in tkm
        assert_m36_main_array(tkm)
        print('PASS v030 M36 SRAM class: proven GBS2/GBJ4 route retained')

        # Same Nintendo EEPROM family marker, different geometry. Both now use
        # one maintainable record-lane engine; geometry changes lane count only.
        ms=td/'ms.gba'; tms=patch(FIX['MetalSlug'],'m36',ms)
        ab=td/'astro.gba'; tab=patch(FIX['Astro'],'m36',ab)
        assert 'EEPROM512 / 512 bytes' in tms and 'EEPROM records/dword:  64' in tms
        assert 'EEPROM8K / 8192 bytes' in tab and 'Storage mode:  EEPROM_COMPACT_AB' in tab and 'EEPROM records/dword:  12' in tab
        assert 'GBJ3 direct payload' not in tab
        assert_m36_main_array(tms,allow_small_logical=True)
        assert_m36_main_array(tab,allow_small_logical=True)
        print('PASS EEPROM geometry: EEPROM512 keeps proven record lanes; EEPROM8K uses compact A/B engine')

        # The v0.30 compact FLASH512 route was subsequently falsified by
        # hardware (zero NOR mutation) and is intentionally superseded by the
        # v0.31 generic FLASH512 gate. Keep the proven FLASH1M compact control.
        lg=td/'lg.gba'; tlg=patch(FIX['LeafGreen'],'m36',lg)
        assert 'FLASH1M / 131072 bytes' in tlg
        assert compact_sector_count(lg.read_bytes(),tlg)==32,compact_sector_count(lg.read_bytes(),tlg)
        assert_m36_main_array(tlg)
        print('PASS v030 FLASH1M geometry/control: compact FLASH1M remains 32 sectors')

        # Deterministic output for the v0.30 EEPROM class that remains current.
        for name,src in [('Astro',FIX['Astro'])]:
            a=td/(name+'1.gba'); b=td/(name+'2.gba')
            patch(src,'m36',a); patch(src,'m36',b)
            assert a.read_bytes()==b.read_bytes(),name
        print('PASS v030 changed-class deterministic generation')

if __name__=='__main__': main()
