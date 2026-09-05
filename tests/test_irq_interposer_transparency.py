#!/usr/bin/env python3
from pathlib import Path
import re, subprocess, tempfile
ROOT=Path(__file__).resolve().parents[1]
SRC=(ROOT/'runtime/sram_hotkey_irq.S').read_text()
CPP=(ROOT/'runtime/gba_runtime.cpp').read_text()
PATCHER=(ROOT/'src/save_patcher.cpp').read_text()
ELF=ROOT/'build_runtime/runtime.elf'
from test_support import GBASAVEHANDLER_BIN as BIN
# Legacy/private-shadow IRQ keeps the conservative full scratch frame, but the
# game-owned SRAM path deliberately restores the hardware-proven v0.16 ABI:
# small scratch-register interposer + direct tail chain.
assert 'GBASH_IRQ_FRAME_ENTER' in SRC
legacy=re.search(r'gbashSramHotkeyIrqEntry:(.*?).size gbashSramHotkeyIrqEntry',SRC,re.S); assert legacy
assert 'GBASH_IRQ_FRAME_ENTER' in legacy.group(1)
for sym in ('gbashSramOwnedShadowInitIrqEntry','gbashSramOwnedShadowHotkeyIrqEntry','gbashSramOwnedShadowHotkeyLatchedIrqEntry'):
    m=re.search(rf'{sym}:(.*?).size {sym}',SRC,re.S); assert m,sym
    assert 'GBASH_IRQ_FRAME_ENTER' not in m.group(1),sym
owned_chain=SRC[SRC.index('.Lowned_chain:'):SRC.index('/* Legacy/private-shadow SRAM route.',SRC.index('.Lowned_chain:'))]
assert 'gbashSramChainSlotAddressWord' in owned_chain
assert 'gbashSramFallbackDispatcherWord' in owned_chain
assert 'ldr r0, =0x04000000' in owned_chain and 'bx r3' in owned_chain
assert '.equ BIOS_ORIGINAL_IRQ_SLOT' not in SRC
assert 'constexpr uptr kBiosIrqOriginalSlot' not in CPP
assert '0x03007FF4u' not in PATCHER
assert 'gbashSramIrqChainTarget' not in owned_chain
# Game-owned SRAM now deliberately owns the BIOS vector from reset, while every
# instruction-proven game write to 03007FFC is redirected to a guarded EWRAM
# chainTarget. The BIOS user IRQ pointer remains the durable one-shot INIT/ARMED state.
boot=re.search(r'u32 gbashSramBootInit\(\)(.*?)(?=__attribute__\(\(section\(".text.runtime"\), used\)\)\nu32 gbashSramOwnedShadowEnsureInitialized)',CPP,re.S)
assert boot
assert 'kBiosUserIrqVector' in boot.group(1)
assert 'gbashSramOwnedShadowInitIrqEntry' in boot.group(1)
ensure=re.search(r'static void ensureOwnedSramShadowInitialized\(\)(.*?)(?=__attribute__\(\(noinline\)\) static u32 commitOwnedSramSnapshot)',CPP,re.S)
assert ensure
body=ensure.group(1)
assert 'if (vector != init)' in body
assert body.index('if (vector != init)') < body.index('restoreOwnedSramShadowFromSnapshot()')
assert body.index('return;', body.index('if (vector != init)')) < body.index('restoreOwnedSramShadowFromSnapshot()'), 'only INIT vector may perform NOR restore'
assert 'state.magic == kSramRuntimeStateMagic && state.initialized == 1u' not in body, 'EWRAM clear must not re-arm cold restore'
assert 'saveLibrary.sramRuntimeStateAddress + 12u' in PATCHER
assert 'originalRom.u32(literalOffset) != 0x03007FFCu' in PATCHER
assert ELF.exists()
dis=subprocess.check_output(['/usr/local/swift/usr/bin/llvm-objdump','-d','--triple=armv4t-none-eabi',str(ELF)],text=True)
assert '<gbashSramOwnedShadowHotkeyIrqEntry>' in dis
# Exact WL4 oracle: state must be outside its 02038000..0203FFFF save image and
# normal full SRAM reads must not have the special forced-refresh capability.
wl4=Path('/mnt/data/wl4_compare/Wario Land 4 (USA, Europe)_RECONSTRUCTED_VANILLA.gba')
if BIN.exists() and wl4.exists():
    with tempfile.TemporaryDirectory() as td:
        out=Path(td)/'wl4.gba'
        text=subprocess.check_output([str(BIN),'patch',str(wl4),str(out),'--nor-profile','m6m'],text=True)
        m=re.search(r'SRAM runtime state:\s+0x([0-9A-F]+)',text); assert m
        state=int(m.group(1),16)
        assert state+16 <= 0x02038000 or state >= 0x02040000
        assert 'SRAM preload trigger:' not in text
        assert 'SRAM shadow:           0x02038000 GAME_OWNED_PROVEN' in text
        print(f'PASS exact WL4 dynamic IRQ chain state: {state:#010x}, outside save mirror, forced-refresh OFF')
print('PASS IRQ interposer: v0.16-compatible owned-SRAM ABI + guarded EWRAM dynamic chain; legacy full-frame path retained')
