#!/usr/bin/env python3
"""Regression: v1.3 hardware-proven D137/MX26 native runtime paths remain frozen in v1.4."""
from pathlib import Path
import hashlib, json, re

ROOT=Path(__file__).resolve().parents[1]
RUNTIME=(ROOT/'runtime/gba_runtime.cpp').read_text()
STACK=(ROOT/'runtime/sram_stack_worker.S').read_text()
IRQ=(ROOT/'runtime/sram_hotkey_irq.S').read_text()
PATCHER=(ROOT/'src/save_patcher.cpp').read_text()
RUNTIME_IMAGE=(ROOT/'src/runtime_image.cpp').read_text()
BUILD_RUNTIME=(ROOT/'tools/build_runtime.py').read_text()

def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()

def runtime_blob_sha(header):
    text=header.read_text()
    body=text.split('kRuntimeBlob[] = {',1)[1].split('};',1)[0]
    blob=bytes(int(x,16) for x in re.findall(r'0x([0-9A-Fa-f]{2})',body))
    return hashlib.sha256(blob).hexdigest()

def main():
    # Exact legacy M6/M36 runtime blob remains the v1.3 hardware-proven blob.
    legacy=ROOT/'generated/runtime_blob.h'
    extended=ROOT/'generated/runtime_blob_extended.h'
    assert runtime_blob_sha(legacy)=='e1031e63e15385a0ad1e32edf93689be7d3dbb0f54713e3e2816843b9a90b1f0'
    assert runtime_blob_sha(extended)=='4addfc123fa266193065bed407ba8f2ba1179a30e92516c089500221a6f864b8'
    assert 'generated_runtime_extended' in extended.read_text()
    assert 'runtime_blob_extended.h' in BUILD_RUNTIME and 'generated_runtime_extended' in BUILD_RUNTIME
    assert 'NorFlashType::IntelStatusRegister ||' in RUNTIME_IMAGE
    assert 'NorFlashType::IntelE8BufferedRww' in RUNTIME_IMAGE
    assert 'GBASH_ADD_ALL_RUNTIME_SYMBOLS(generated_runtime);' in RUNTIME_IMAGE
    assert 'GBASH_ADD_ALL_RUNTIME_SYMBOLS(generated_runtime_extended);' in RUNTIME_IMAGE

    d=json.loads((ROOT/'config/nor/chips/m6mgd137.json').read_text())
    dp=json.loads((ROOT/'config/nor/protocols/intel-relative-word40.json').read_text())
    assert d['capacity_bytes']==0x1000000 and d['protocol_ref']=='intel-relative-word40'
    assert 'm6mjd137' in d['aliases']
    assert dp['driver_enum']=='IntelRelativeWordProgram'
    assert dp['program_command']==0x40 and dp['runtime_program_selector']==0x140
    assert dp['supports_block_erase'] is True and dp['erase_block_bytes']==0x10000
    assert dp['storage_forbidden_ranges']==[{'offset':0x7F0000,'bytes':0x20000}]

    m=json.loads((ROOT/'config/nor/chips/mx26l6420mc90.json').read_text())
    mp=json.loads((ROOT/'config/nor/protocols/amd-unlock-word.json').read_text())
    assert m['capacity_bytes']==0x800000 and m['protocol_ref']=='amd-unlock-word'
    assert mp['driver_enum']=='AmdUnlockWordProgram' and mp['program_command']==0xA0
    assert mp['supports_block_erase'] is False and mp['erase_block_bytes']==0

    for token in ('programSelector == 0x0140u','*target = 0x0070u','*target = 0x0040u',
                  '*target = 0x0060u','*target = 0x0020u','0x0038u'):
        assert token in RUNTIME, token
    assert 'sramStackD137ProgramChunk' in STACK and 'sramStackD137EraseBlock' in STACK
    assert 'sramStackD137ProgramChunk' in IRQ and 'sramStackD137EraseBlock' in IRQ

    for token in ('0x08000AAAu','0x08000554u','0x00AAu','0x0055u','0x00A0u','0x0040u','0x0020u','0x00F0u'):
        assert token in RUNTIME, token
    mx_erase=RUNTIME.split('if (programCommand == 0x00A0u) {',3)[3].split('}',1)[0]
    assert 'ramLeaveNorCritical(&state);' in mx_erase and 'return 0xE2A0u;' in mx_erase
    assert '*target =' not in mx_erase
    assert 'sramStackAmdProgramChunk' in STACK and 'sramStackAmdProgramChunk' in IRQ

    assert '!backend.supportsBlockErase' in PATCHER
    assert 'requiresProgramOnlyFallback' in PATCHER
    assert 'tryCreateAppendedProgramOnlyJournalLayout' in PATCHER
    assert 'selected NOR has no sector erase and no program-only SRAM journal fits' in PATCHER
    assert 'backend.storageForbiddenRangeCount' in PATCHER

    print('PASS v1.3.1 regression: hardware-proven M6/M36/D137/MX26 runtime paths remain frozen under v1.4 profiles')

if __name__=='__main__': main()
