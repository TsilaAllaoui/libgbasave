#!/usr/bin/env python3
from pathlib import Path
import hashlib,json
R=Path(__file__).resolve().parents[1]
asm=(R/'runtime/direct_rww/reference/flash_compact_rww_payload.S').read_text()
manifest=json.loads((R/'data/direct_rww_assets/manifest.json').read_text())
asset=R/'data/direct_rww_assets/flash_compact_rww_payload.bin'
a=manifest['assets']['flash_compact_rww_payload']
def ck(n,c):
    if not c: raise SystemExit('FAIL: '+n)
    print('PASS:',n)
ck('audio-only lib identity','kLibraryDevelopmentRevision = 10u' in (R/'include/gbasave/version.h').read_text() and '1.0.0-dev10-audio-only' in (R/'include/gbasave/version.h').read_text())
ck('asset hash exact',hashlib.sha256(asset.read_bytes()).hexdigest()=='de10e86aea9563b388787fac6b0157ce88a21ccda51801765873f4e9f84a1f29')
ck('manifest hash exact',a['sha256']=='de10e86aea9563b388787fac6b0157ce88a21ccda51801765873f4e9f84a1f29')
ck('worker ABI',a['dispatch_offset']==52 and a['worker_start']==680 and a['worker_end']==1560)
ck('payload bounded',asset.stat().st_size==7456 and asset.stat().st_size<8192)
ck('FIFO DMA source safety','DMA1SAD programmed source' in asm and 'DMA2SAD programmed source' in asm and asm.count('ldr r6, =0x08000000')>=3)
ck('FIFO targets checked','.Lds_fifo_a1:' in asm and '.Lds_fifo_b1:' in asm and '.Lds_fifo_a2:' in asm and '.Lds_fifo_b2:' in asm)
ck('RAM FIFO DMA keep bits','orr r8, r8, #0x100' in asm and 'orr r8, r8, #0x200' in asm)
ck('feed timers preserved','orreq r8, r8, #1' in asm and 'orrne r8, r8, #2' in asm)
ck('IME remains off','bl m36c_worker_start' in asm and 'ldr r4, =0x04000208' in asm)
ck('DEV12 32-byte E8 cadence retained','mov r6, #15' in asm and 'cmp r8, #32' in asm and 'moveq r12, #64' not in asm)
print('libgbasave audio-only FLASH compact runtime contract PASS')
