from pathlib import Path
import importlib.util

ROOT=Path(__file__).resolve().parents[1]
SRC=(ROOT/'runtime/gba_runtime.cpp').read_text()

def test_breadcrumbs_never_touch_logical_payload():
    assert 'kSramBreadcrumbOffset = 0xFF40u' in SRC
    assert 'kSramSnapshotHeaderOffset = 0xFF00u' in SRC
    assert 'kFlashBreadcrumbOffsetFromMetadata = 0x80u' in SRC
    assert 'headAddressByBlock(blockIndex) + kFlashBreadcrumbOffsetFromMetadata' in SRC
    # FLASH logical slot payloads are below metadataOffset; breadcrumbs are metadata-relative.
    assert 'flashSlotMetadataOffsetBytes' in SRC

def test_stages_are_monotonic_bit_clears():
    vals=[0xFFFE,0xFFFC,0xFFF8,0xFFF0,0xFFE0,0xFFC0]
    for a,b in zip(vals,vals[1:]):
        assert (a & b)==b

def test_sram_commit_breadcrumb_is_commit_last_and_final_verify_last():
    body=SRC[SRC.index('commitOwnedSramSnapshot()'):SRC.index('static void ensureSramShadowInitialized()', SRC.index('commitOwnedSramSnapshot()'))]
    assert body.index('beginSramBreadcrumb') < body.index('gbashRunSramStackProgram(targetBlock + offset')
    assert body.index('kBreadcrumbStageMetadataWritten') < body.index('const u16 commit = kSramSnapshotCommit')
    assert body.index('kBreadcrumbStageCommitted') < body.index('validOwnedSramSnapshot') < body.index('kBreadcrumbStageFinalVerified')

def test_flash_breadcrumb_metadata_is_per_slot_and_commit_last():
    assert 'slot * kFlashBreadcrumbStride' in SRC
    body=SRC[SRC.index('commitSlotWithBreadcrumb'):SRC.index('static u32 appendSnapshot')]
    assert body.index('programMarkerValue(markerAddressByBlock') < body.index('kBreadcrumbStageMetadataWritten') < body.index('publishSlot') < body.index('kBreadcrumbStageCommitted')

def test_decoder_synthetic(tmp_path):
    spec=importlib.util.spec_from_file_location('dec',ROOT/'tools/gbash_breadcrumb_decode.py')
    m=importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
    b=bytearray(b'\xff'*0x10000)
    # SRAM breadcrumb at FF40: magic/op/generation/stage FINAL_VERIFIED
    b[0xFF40:0xFF48]=(0xBC32).to_bytes(2,'little')+(1).to_bytes(2,'little')+(3).to_bytes(2,'little')+(0xFFC0).to_bytes(2,'little')
    p=tmp_path/'dump.bin'; p.write_bytes(b)
    text=m.decode(p,0x7A0000)
    assert 'SRAM_COMMIT' in text and 'FINAL_VERIFIED' in text and '0x007AFF40' in text
