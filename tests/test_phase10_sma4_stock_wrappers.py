#!/usr/bin/env python3
from pathlib import Path
import subprocess, struct
ROOT=Path(__file__).resolve().parents[1]
ROM=Path('/mnt/data/holefix_testroms/SMA4.gba')
OUT=Path('/tmp/gbash_phase10_sma4.gba')
from test_support import GBASAVEHANDLER_BIN as BIN

def main():
    if not ROM.exists():
        print('SKIP FLASH1M exact-ROM regression: SMA4 fixture unavailable')
        return
    subprocess.check_call([str(BIN),'patch',str(ROM),str(OUT),'--nor-profile','m6m'],stdout=subprocess.DEVNULL)
    vanilla=ROM.read_bytes(); patched=OUT.read_bytes()
    preserved={
      'FlashTimerIntr':(0xE76C4,0x28), 'SetFlashTimerIntr':(0xE76EC,0x3C),
      'StartFlashTimer':(0xE7728,0xA8), 'StopFlashTimer':(0xE77D0,0x44),
      'ReadFlash1':(0xE7814,0x04), 'SetReadFlash1':(0xE7818,0x40),
      'ReadFlash':(0xE787C,0x9C), 'VerifyFlashSector':(0xE7948,0x98),
      'VerifyFlashSectorNBytes':(0xE79E0,0x98),
      'ProgramFlashSectorAndVerify':(0xE7A78,0x44),
      'ProgramFlashSectorAndVerifyNBytes':(0xE7ABC,0x48),
      'IdentifyFlash':(0xE7B04,0x94), 'WaitForFlashWrite':(0xE7B98,0xA0),
      'ProgramFlashByte':(0xE7D78,0x38),
    }
    for name,(off,n) in preserved.items():
        assert patched[off:off+n]==vanilla[off:off+n], name
    changed=[(0xE7604,0x1C),(0xE7628,0x9C),(0xE7858,0x24),(0xE7918,0x30),(0xE7C38,0x74),(0xE7CAC,0xCC),(0xE7DB0,0xA4)]
    for off,n in changed: assert patched[off:off+n]!=vanilla[off:off+n]
    # RuntimeConfig v12: currentBankStateAddress follows forcedSetupProfileAddress
    # and the new NOR program-command capability fields. Keep this exact ABI gate.
    cfg=0x400000
    assert patched[cfg:cfg+4]==b'GSVH'
    current_bank=struct.unpack_from('<I',patched,cfg+0x3C)[0]
    assert current_bank==0x0203BFF0, hex(current_bank)
    assert current_bank!=0x030079E0
    # epoch anchor must force an erase relative to old 0000-anchor builds.
    for block in range(40):
        base=0x410000+block*0x10000
        assert patched[base+0xFFFC:base+0xFFFE]==b'\xAA\xAA'
    print('PASS Phase10 SMA4: Nintendo public FLASH wrappers preserved exact')
    print('PASS Phase10 SMA4: bank state moved off 0x030079E0 ReadFlash1 pointer')
    print('PASS Phase10 SMA4: 40 save blocks use 0xAAAA erase-forcing epoch anchors')
if __name__=='__main__': main()
