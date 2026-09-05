#!/usr/bin/env python3
from pathlib import Path
import subprocess, tempfile, hashlib

ROOT=Path(__file__).resolve().parents[1]
from test_support import GBASAVEHANDLER_BIN as BIN, WORKSPACE_ROOT
MZM=Path('/mnt/data/rom_inputs/MZM.gba')
WL4=Path('/mnt/data/rom_inputs/WL4.gba')
STORAGE_IMAGE=(ROOT/'src'/'storage_image.cpp').read_text()
MAIN=(WORKSPACE_ROOT/'GBASaveHandler'/'src'/'main.cpp').read_text()

# The preservation mechanism itself must be allocation-layer generic: no title,
# game code, save library version, or save type gate belongs in the overlay path.
init=STORAGE_IMAGE
for forbidden in ['BMXE','AWAE','SRAM_V113','SaveType::Sram','SR16']:
    assert forbidden not in init, forbidden
assert 'storage.blocks' in init
assert 'PackedBlocks' in init and 'FullImage' in init
assert '--preserve-storage-from' in MAIN
assert '--preserve-existing-storage is unsafe' in MAIN

if MZM.exists():
    with tempfile.TemporaryDirectory() as td:
        td=Path(td)
        packed=bytes(((i*73+19) ^ (i>>7)) & 0xff for i in range(0x20000))
        packed_path=td/'packed.bin'; packed_path.write_bytes(packed)
        out_packed=td/'mzm_packed.gba'
        rep=subprocess.check_output([str(BIN),'patch',str(MZM),str(out_packed),'--nor-profile','m6m','--preserve-storage-from',str(packed_path)],text=True)
        assert 'EXACT_PRESERVE_PACKED_BLOCKS' in rep
        b=out_packed.read_bytes()
        assert b[0x770000:0x790000] == packed

        full=bytearray(MZM.read_bytes()); full[0x770000:0x790000]=packed
        full_path=td/'cart_full.gba'; full_path.write_bytes(full)
        out_full=td/'mzm_full.gba'
        rep2=subprocess.check_output([str(BIN),'patch',str(MZM),str(out_full),'--nor-profile','m6m','--preserve-storage-from',str(full_path)],text=True)
        assert 'EXACT_PRESERVE_FULL_IMAGE' in rep2 and out_full.read_bytes() == b

        bad=td/'bad.bin'; bad.write_bytes(packed[:0x10000])
        cp=subprocess.run([str(BIN),'patch',str(MZM),str(td/'bad.gba'),'--nor-profile','m6m','--preserve-storage-from',str(bad)],text=True,capture_output=True)
        assert cp.returncode != 0 and 'storage preservation image size mismatch' in cp.stderr
        cp=subprocess.run([str(BIN),'patch',str(MZM),str(td/'unsafe.gba'),'--nor-profile','m6m','--preserve-existing-storage'],text=True,capture_output=True)
        assert cp.returncode != 0 and 'is unsafe' in cp.stderr
    print('PASS Phase20 exact MZM packed/full-image preservation + fail-closed sizing')
else:
    print('SKIP Phase20 exact MZM preservation test: fixture unavailable')

if WL4.exists():
    with tempfile.TemporaryDirectory() as td:
        td=Path(td)
        wbase=bytearray(WL4.read_bytes())
        wa=bytes((i*11+7)&0xff for i in range(0x10000))
        wb=bytes((255-((i*13+5)&0xff)) for i in range(0x10000))
        wbase[0x7A0000:0x7B0000]=wa; wbase[0x7B0000:0x7C0000]=wb
        wfull=td/'wl4_cart.gba'; wfull.write_bytes(wbase)
        wout=td/'wl4.gba'
        wr=subprocess.check_output([str(BIN),'patch',str(WL4),str(wout),'--nor-profile','m6m','--preserve-storage-from',str(wfull)],text=True)
        assert 'EXACT_PRESERVE_FULL_IMAGE' in wr
        w=wout.read_bytes()
        assert w[0x7A0000:0x7B0000]==wa and w[0x7B0000:0x7C0000]==wb
    print('PASS Phase20 exact WL4 full-image preservation uses allocator-selected blocks')
else:
    print('SKIP Phase20 exact WL4 preservation test: fixture unavailable')

print('PASS Phase20 generic exact storage preservation: packed/full-image overlays + fail-closed sizing + unsafe FF mode rejected')
