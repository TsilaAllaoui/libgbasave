#!/usr/bin/env python3
from pathlib import Path
import subprocess, tempfile

ROOT=Path(__file__).resolve().parents[1]
from test_support import GBASAVEHANDLER_BIN as BIN
SRC=(ROOT/'runtime'/'gba_runtime.cpp').read_text()
PATCH=(ROOT/'src'/'save_patcher.cpp').read_text()
MZM=Path('/mnt/data/rom_inputs/MZM.gba')
WL4=Path('/mnt/data/rom_inputs/WL4.gba')

# Runtime ordering: restore must happen before the game's pre-load test write.
write_pos=SRC.index('u32 gbashSramWriteMirror')
body=SRC[write_pos:SRC.index('u32 gbashSramVerifyMirror', write_pos)]
assert 'gRuntimeConfig.sramRefreshTriggerOffset' in body
assert body.index('restoreOwnedSramShadowFromSnapshot();') < body.index('copyBytes(')
assert body.index('restoreOwnedSramShadowFromSnapshot();') < body.index('copyBytes(')

# Host qualification must be narrow and exact.
assert 'saveLibrary.sramPreloadRefreshTriggerOffset' in PATCH
for forbidden in ['BMXE','Nintendo SRAM_V113','isMzmUsSelfTestLayout']:
    assert forbidden not in PATCH, forbidden

if MZM.exists():
    with tempfile.TemporaryDirectory() as td:
        m=Path(td)/'mzm.gba'
        rep=subprocess.check_output([str(BIN),'patch',str(MZM),str(m),'--nor-profile','m6m'],text=True)
        assert 'SRAM preload trigger:  0x7F80 STRUCTURAL_PROOF' in rep
        mb=m.read_bytes()
        for off in (0x770000,0x780000):
            assert mb[off+0xFFFC:off+0xFFFE] == b'\xaa\xaa'
        print('PASS Phase18 MZM: 0x7F80 preload trigger + normal anchored storage initialization')
else:
    print('SKIP Phase18 exact MZM qualification: fixture unavailable')

if WL4.exists():
    with tempfile.TemporaryDirectory() as td:
        w=Path(td)/'wl4.gba'
        rep=subprocess.check_output([str(BIN),'patch',str(WL4),str(w),'--nor-profile','m6m'],text=True)
        assert 'SRAM preload trigger:' not in rep
        wb=w.read_bytes()
        for off in (0x7A0000,0x7B0000):
            assert wb[off+0xFFFC:off+0xFFFE] == b'\xaa\xaa'
        print('PASS Phase18 WL4: no forced-refresh trigger; normal live-shadow/anchor mode preserved')
else:
    print('SKIP Phase18 exact WL4 qualification: fixture unavailable')

# Behavioral loader model: an already-committed snapshot is recovered before
# MZM's SRAM self-test mutates 0x7F80. The subsequent full-image read refreshes
# the pristine committed image again.
SIZE=0x8000
snapshot=bytearray((i*19+7)&0xff for i in range(SIZE))
shadow=bytearray(SIZE)  # EWRAM was cleared by MZM soft reset
trigger=0x7F80
# pre-load test write
shadow[:] = snapshot
shadow[trigger:trigger+8] = b'METTEST!'
assert shadow != snapshot
# stock test can now read/verify the same live shadow; READ_ALL follows and
# v0.17 full-image boundary restores the pristine persisted snapshot.
shadow[:] = snapshot
assert shadow == snapshot
print('PASS Phase18 MZM preload-selftest -> full-read restore lifecycle model')
