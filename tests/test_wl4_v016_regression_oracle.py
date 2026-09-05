#!/usr/bin/env python3
"""Exact Wario Land 4 hardware-regression oracle for v0.29.

The user-supplied working WL4_Patched.gba is byte-identical to v0.16.  M6M
must retain that game-facing SRAM lifecycle.  M36 deliberately uses the older
hardware-proven GBS2/GBJ4 compatibility payload selected by the exact shared
GBABR plan instead of the generic v0.28 runtime that failed on hardware.
"""
from pathlib import Path
import hashlib, struct, subprocess, tempfile, re

ROOT = Path(__file__).resolve().parents[1]
from test_support import GBASAVEHANDLER_BIN as BIN
VANILLA = Path('/mnt/data/rom_inputs/WL4.gba')
WORKING_V016 = Path('/mnt/data/wl4_compare/WL4_Patched.gba')
VANILLA_SHA = 'd16c7bf6e62bb84049fff1b387108fbd1e6e2cd38ca994ab5310dd9cbf9ba414'
WORKING_SHA = 'a60b87ea3e2fc8f480ec8070eb5b2994f98e0f2fee78b19fea2836fb4a127305'
CONFIG_MAGIC = b'GSVH'


def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()
def patch(src, profile, out):
    return subprocess.check_output([str(BIN),'patch',str(src),str(out),'--nor-profile',profile], text=True)


def test_m6():
    with tempfile.TemporaryDirectory() as td:
        td=Path(td); a=td/'a.gba'; b=td/'b.gba'
        ra=patch(VANILLA,'m6m',a); patch(VANILLA,'m6m',b)
        assert a.read_bytes()==b.read_bytes()
        rom=a.read_bytes(); assert len(rom)==0x800000
        off=rom.find(CONFIG_MAGIC); assert off>=0
        assert struct.unpack_from('<I',rom,off+4)[0]==13
        assert struct.unpack_from('<H',rom,off+0x10)[0]==0x40
        assert struct.unpack_from('<I',rom,off+0x60)[0]==0x02038000
        assert struct.unpack_from('<I',rom,off+0x68)[0]==0x8000
        assert struct.unpack_from('<H',rom,off+0x72)[0]==0x0001
        assert struct.unpack_from('<I',rom,off+0x74)[0]==0xFFFFFFFF
        assert 'SRAM shadow:           0x02038000 GAME_OWNED_PROVEN' in ra
        assert 'SRAM runtime state:    0x02037FE0 GUARDED_EWRAM_CHAIN_STATE' in ra
        assert 'SRAM preload trigger:' not in ra
        blocks=[(int(x,16),int(y,16)) for x,y in re.findall(r'block \d+ -> 0x([0-9A-F]+)\.\.0x([0-9A-F]+)',ra)]
        assert blocks==[(0x7A0000,0x7AFFFF),(0x7B0000,0x7BFFFF)],blocks
        print('PASS WL4 M6M: exact vanilla, v0.16 live-shadow semantics, refresh OFF, deterministic',sha(a))


def test_m36():
    with tempfile.TemporaryDirectory() as td:
        td=Path(td); a=td/'a.gba'; b=td/'b.gba'
        ra=patch(VANILLA,'m36',a); patch(VANILLA,'m36',b)
        assert a.read_bytes()==b.read_bytes()
        rom=a.read_bytes(); assert len(rom)==0x940000
        # Compatibility route is intentionally not the generic GSVH runtime and
        # is byte-identical to the recovered hardware-lineage R37A planner.
        assert CONFIG_MAGIC not in rom[0x800000:0x820000]
        assert 'M36 RWW runtime bank:  8' in ra
        assert 'SRAM shadow:           0x0E000000 PRIVATE_COMPATIBILITY' in ra
        blocks=[(int(x,16),int(y,16)) for x,y in re.findall(r'block \d+ -> 0x([0-9A-F]+)\.\.0x([0-9A-F]+)',ra)]
        assert blocks==[(0x900000,0x91FFFF),(0x920000,0x93FFFF)],blocks
        assert sha(a)=='d1b21afdf203a73793046b76fbb729a23e13587a3877a946182dca849f109e2b',sha(a)
        # Exact GBABR IRQ sites are redirected to the historical GBS2 chain slot.
        raw=VANILLA.read_bytes()
        changed=[]
        for i in range(0,len(raw)-3,4):
            if raw[i:i+4]==b'\xfc\x7f\x00\x03' and rom[i:i+4]==b'\xf4\x7f\x00\x03': changed.append(i)
        assert changed, 'no exact IRQ-vector literal redirects found'
        # The special route must not globally rewrite SRAM 0x0E literals to EWRAM.
        assert 'SRAM direct literals:  0' in ra
        print('PASS WL4 M36: exact shared-plan GBS2/GBJ4 route, contiguous 0x40000 retention, deterministic',sha(a))


if __name__=='__main__':
    assert BIN.exists(),BIN
    if not VANILLA.exists() or not WORKING_V016.exists():
        print('SKIP WL4 exact v0.16 oracle unavailable'); raise SystemExit(0)
    assert sha(VANILLA)==VANILLA_SHA,sha(VANILLA)
    assert sha(WORKING_V016)==WORKING_SHA,sha(WORKING_V016)
    print('PASS WL4 oracle provenance: exact vanilla + user hardware-working v0.16 candidate')
    test_m6(); test_m36()
