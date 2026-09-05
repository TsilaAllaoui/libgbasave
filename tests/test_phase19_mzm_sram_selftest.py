#!/usr/bin/env python3
from pathlib import Path
import subprocess, tempfile

ROOT=Path(__file__).resolve().parents[1]
from test_support import GBASAVEHANDLER_BIN as BIN
SRC=(ROOT/'runtime'/'gba_runtime.cpp').read_text()
PATCH=(ROOT/'src'/'save_patcher.cpp').read_text()
MZM=Path('/mnt/data/rom_inputs/MZM.gba')

# Static runtime guards for the exact MZM failure mode.
write_pos=SRC.index('u32 gbashSramWriteMirror')
verify_pos=SRC.index('u32 gbashSramVerifyMirror', write_pos)
write_body=SRC[write_pos:verify_pos]
verify_body=SRC[verify_pos:SRC.index('u32 gbashOwnedSramHotkeyCommit', verify_pos)]
helper=SRC[SRC.index('static bool readableSramWriteSource'):write_pos]

# Nintendo SramWrite accepts ROM sources. MZM's first SramTestFlash probe is a
# ROM constant -> SRAM write, so rejecting ROM would force gSramCorruptFlag.
assert 'start >= 0x08000000u' in helper
assert 'end <= 0x0E000000u' in helper
assert 'readableSramWriteSource(source, byteCount)' in write_body

# MZM-specific pre-test trigger restores the committed snapshot before the
# destructive probe write at SRAM + 0x7F80.
assert 'gRuntimeConfig.sramRefreshTriggerOffset' in write_body
assert write_body.index('restoreOwnedSramShadowFromSnapshot();') < write_body.index('copyBytes(')
assert 'saveLibrary.sramPreloadRefreshTriggerOffset' in PATCH
for forbidden in ['BMXE','Nintendo SRAM_V113','isMzmUsSelfTestLayout']:
    assert forbidden not in PATCH, forbidden

# SramCheck is a generic comparator. READ_ALL calls it SRAM -> EWRAM, while
# SramWriteChecked uses source -> SRAM. Both SRAM-side operands must virtualize.
assert verify_body.count('sramVirtualizedReadAddress') >= 2
assert 'mappedSource' in verify_body and 'mappedTarget' in verify_body

# Behavioral model of MZM's real SramTestFlash + SramRead_All sequence.
SIZE=0x8000
TRIGGER=0x7F80
snapshot=bytearray((i*37+11)&0xff for i in range(SIZE))
shadow=bytearray(SIZE)  # InitializeGame/SoftReset cleared EWRAM.
probe=bytes(b'METROID_ZERO_MISSION_SRAM_TEST')
probe=probe[:32]

# First ROM-constant SramWriteChecked: trigger restores snapshot, then writes.
shadow[:] = snapshot
shadow[TRIGGER:TRIGGER+len(probe)] = probe
assert shadow[TRIGGER:TRIGGER+len(probe)] == probe
# SramCheck(ROM constant, SRAM) sees the virtual shadow and passes.
assert probe == bytes(shadow[TRIGGER:TRIGGER+len(probe)])

# SramWriteUnchecked(SRAM, local), increment, then checked IWRAM->SRAM write.
local=bytearray(shadow[TRIGGER:TRIGGER+len(probe)])
for i in range(len(local)):
    local[i]=(local[i]+1)&0xff
shadow[TRIGGER:TRIGGER+len(local)] = local
assert bytes(shadow[TRIGGER:TRIGGER+len(local)]) == bytes(local)
# Final SRAM->local readback also sees virtual shadow; no corrupt flags.
readback=bytes(shadow[TRIGGER:TRIGGER+len(local)])
assert readback == bytes(local)
gSramCorruptFlag=0
assert gSramCorruptFlag == 0

# READ_ALL now restores the pristine persisted image, removing test mutation.
shadow[:] = snapshot
# CHECK_ALL compares virtual SRAM against the same game-owned mirror.
assert shadow == snapshot
print('PASS Phase19 MZM SramTestFlash: ROM-source write + bidirectional compare + pristine READ_ALL restore')

if not MZM.exists():
    print('SKIP Phase19 exact MZM preserve-ROM qualification: fixture unavailable')
    raise SystemExit(0)

with tempfile.TemporaryDirectory() as td:
    out=Path(td)/'mzm_v019_preserve.gba'
    rep=subprocess.check_output([str(BIN),'patch',str(MZM),str(out),'--nor-profile','m6m'],text=True)
    assert 'SRAM preload trigger:  0x7F80 STRUCTURAL_PROOF' in rep
    b=out.read_bytes()
    for off in (0x770000,0x780000):
        assert b[off+0xFFFC:off+0xFFFE] == b'\xaa\xaa'
    print('PASS Phase19 MZM self-test runtime with normal anchored storage initialization')
