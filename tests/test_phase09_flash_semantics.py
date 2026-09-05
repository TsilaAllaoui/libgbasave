#!/usr/bin/env python3
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]

def main():
    source=(ROOT/'runtime'/'gba_runtime.cpp').read_text()
    assert 'if (unitBytesEqual(logicalUnit, 0u, source, gRuntimeConfig.logicalUnitSizeBytes))' in source
    assert 'if (unitBytesEqual(logicalUnit, patchOffset, patch, patchLength))' in source
    assert 'u32 gbashVerifyFlashCore' in source
    assert 'return verifyUnitBytes(logicalSector, 0u, source, gRuntimeConfig.logicalUnitSizeBytes);' in source
    assert '{FlashRoutineRole::VerifyCore, "gbashVerifyFlashCore"}' in (ROOT/'src'/'save_patcher.cpp').read_text()
    print('PASS Phase09: identical FLASH writes are no-op generations')
    print('PASS Phase09: VerifyFlash Core/Sector/NBytes compare logical committed data')
if __name__=='__main__': main()
