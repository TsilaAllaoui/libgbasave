#!/usr/bin/env python3
from pathlib import Path
import subprocess, tempfile
ROOT=Path(__file__).resolve().parents[1]
from test_support import GBASAVEHANDLER_BIN as BIN
ROM=Path('/mnt/data/Metroid Fusion (USA, Australia)(1).gba')
if not ROM.exists():
    print('SKIP Fusion exact-ROM regression: fixture unavailable'); raise SystemExit(0)
with tempfile.TemporaryDirectory() as td:
    out=Path(td)/'fusion.gba'
    report=subprocess.check_output([str(BIN),'patch',str(ROM),str(out),'--nor-profile','m6m','--save-type','sram'],text=True)
    assert 'SRAM shadow:           0x02038000 GAME_OWNED_PROVEN' in report
    assert 'Runtime:       0x7C0000..' in report
    assert 'block 0 -> 0x7A0000..0x7AFFFF' in report
    assert 'block 1 -> 0x7D0000..0x7DFFFF' in report
    original=ROM.read_bytes(); patched=out.read_bytes()
    assert original[0x7B0000:0x7C0000] == patched[0x7B0000:0x7C0000]
    # v10 must leave Nintendo's BIOS IRQ-vector install literals byte-identical.
    for off in (0xFC, 0xAD8):
        assert original[off:off+4] == patched[off:off+4]
        assert int.from_bytes(patched[off:off+4], 'little') == 0x03007FFC
    approved=[
        (0x000000,0x000004),
        (0x004BCC,0x004BD4),(0x004C54,0x004C5C),(0x004C94,0x004C9C),
        (0x7C0000,0x7C0000+(ROOT/'build_runtime/runtime.bin').stat().st_size),
        (0x7AFFFC,0x7AFFFE),(0x7DFFFC,0x7DFFFE),
    ]
    for off in (0xC5C,0xCD4,0xCEC,0xD04,0xD74,0xDCC,0xDE8,0xE28,0xE44,0xCBE0,0x7FB44,0xA5264,0x5BE6E0):
        approved.append((off,off+4))
    def ok(i): return any(a<=i<b for a,b in approved)
    unexpected=[i for i,(a,b) in enumerate(zip(original,patched)) if a!=b and not ok(i)]
    assert not unexpected, [hex(x) for x in unexpected[:20]]
print('PASS Fusion exact regression: proven RAM mirror + lazy fixed-dispatch IRQ chain + stock vector literals + referenced 0x7B block untouched + 0 unexpected diffs')
