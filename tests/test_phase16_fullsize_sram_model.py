#!/usr/bin/env python3
from __future__ import annotations
import copy, hashlib, pathlib, re, struct, subprocess, tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
from test_support import GBASAVEHANDLER_BIN as BIN
RUNTIME_SRC = (ROOT / 'runtime' / 'gba_runtime.cpp').read_text()
IRQ_SRC = (ROOT / 'runtime' / 'sram_hotkey_irq.S').read_text()
WORKER_SRC = (ROOT / 'runtime' / 'sram_stack_worker.S').read_text()
MAGIC = 0x36315253
VERSION = 1
COMMIT = 0xA55A
HEADER = 0xFF00
SAVE = 0x8000
BLOCK = 0x10000

def fnv(data: bytes) -> int:
    h=2166136261
    for b in data:
        h ^= b
        h = (h*16777619)&0xffffffff
    return h

def newer(a:int,b:int)->bool:
    d=(a-b)&0xffffffff
    return d != 0 and d < 0x80000000

def valid(block: bytearray):
    magic,version,size,gen,ginv,h,hinv,commit = struct.unpack_from('<IIIIIIIH', block, HEADER)
    if (magic,version,size,commit)!=(MAGIC,VERSION,SAVE,COMMIT): return None
    if ginv != ((~gen)&0xffffffff) or hinv != ((~h)&0xffffffff): return None
    if fnv(bytes(block[:SAVE])) != h: return None
    return gen

def select(a:bytearray,b:bytearray):
    ga,gb=valid(a),valid(b)
    if ga is None and gb is None: return None
    if ga is not None and (gb is None or not newer(gb,ga)): return ('A',ga,bytes(a[:SAVE]))
    return ('B',gb,bytes(b[:SAVE]))

def commit_actions(block: bytearray, data: bytes, generation: int):
    # Model the runtime's externally visible transaction boundaries. A reused
    # block is erased before programming; all data precedes the header and the
    # exact 16-bit COMMIT marker is the final publication action.
    actions=[]
    if any(x != 0xff for x in block[:SAVE]) or any(x != 0xff for x in block[HEADER:HEADER+32]):
        actions.append(('erase',None))
    for off in range(0,SAVE,0x1000): actions.append(('data',(off,data[off:off+0x1000])))
    h=fnv(data)
    body=struct.pack('<IIIIIII',MAGIC,VERSION,SAVE,generation,(~generation)&0xffffffff,h,(~h)&0xffffffff)
    actions.append(('header',body))
    actions.append(('commit',struct.pack('<H',COMMIT)))
    return actions

def apply(block:bytearray, action):
    kind,payload=action
    if kind=='erase': block[:] = b'\xff'*BLOCK
    elif kind=='data':
        off,data=payload; block[off:off+len(data)]=data
    elif kind=='header': block[HEADER:HEADER+len(payload)] = payload
    elif kind=='commit': block[HEADER+28:HEADER+30] = payload
    else: raise AssertionError(kind)

def full_commit(block:bytearray,data:bytes,gen:int):
    for a in commit_actions(block,data,gen): apply(block,a)
    assert valid(block)==gen

def power_cut_model():
    A=bytearray(b'\xff'*BLOCK); B=bytearray(b'\xff'*BLOCK)
    old=bytes((i*13+7)&0xff for i in range(SAVE))
    mid=bytes((i*29+3)&0xff for i in range(SAVE))
    new=bytes((i*47+11)&0xff for i in range(SAVE))
    full_commit(A,old,0xffffffff)
    full_commit(B,mid,0)
    assert select(A,B)[2] == mid  # rollover 0 is newer than FFFFFFFF
    actions=commit_actions(A,new,1)  # A reuse includes erase
    for cut in range(len(actions)+1):
        a=copy.deepcopy(A); b=copy.deepcopy(B)
        for act in actions[:cut]: apply(a,act)
        chosen=select(a,b)
        assert chosen is not None
        if cut < len(actions):
            assert chosen[2] == mid, (cut,chosen[0],chosen[1])
        else:
            assert chosen[2] == new and chosen[1] == 1
    # Torn/corrupt data after a published-looking header must fail hash and
    # fall back to the previous generation.
    bad=copy.deepcopy(A)
    for act in actions: apply(bad,act)
    bad[0x1234] ^= 1
    assert select(bad,B)[2] == mid
    print(f'PASS Phase16 A/B snapshot power-cut model: {len(actions)+1} cut boundaries')
    print('PASS Phase16 generation rollover + CRC/hash fallback model')

def static_runtime_audit():
    assert 'kSramConfigGameOwnedShadowSnapshot' in RUNTIME_SRC
    assert 'generationIsNewer' in RUNTIME_SRC
    assert 'kSramSnapshotHeaderOffset = 0xFF00u' in RUNTIME_SRC
    assert 'kSramSnapshotChunkBytes = 0x1000u' in RUNTIME_SRC
    assert 'header.commit = 0xFFFFu' in RUNTIME_SRC
    # COMMIT must be programmed after header body and chunk loop inside the
    # reusable A/B transaction itself. The separate program-only journal has
    # its own commit-last sequence and must not affect this source-order gate.
    txn=re.search(r'commitOwnedSramSnapshot\(\)(.*?)(?=\nstatic void ensureSramShadowInitialized)',RUNTIME_SRC,re.S)
    assert txn
    body=txn.group(1)
    assert body.index('const u16 commit = kSramSnapshotCommit') > body.index('headerBodyBytes') > body.index('for (u32 offset = 0u; offset < gRuntimeConfig.sramSizeBytes')
    # INIT IRQ must never touch game-owned EWRAM; first patched SRAM API does.
    m=re.search(r'gbashSramOwnedShadowInitIrqEntry:(.*?).size gbashSramOwnedShadowInitIrqEntry', IRQ_SRC, re.S)
    assert m and 'gbashSramOwnedShadowEnsureInitialized' not in m.group(1)
    assert 'GBASH_IRQ_FRAME_ENTER' not in m.group(1) and 'b .Lowned_chain' in m.group(1)
    assert 'gbashSramChainSlotAddressWord' in IRQ_SRC and 'gbashSramFallbackDispatcherWord' in IRQ_SRC
    assert 'ensureSramShadowInitialized();' in RUNTIME_SRC
    # Separate worker is PIC and source itself contains no fixed EWRAM address.
    assert '0x020' not in WORKER_SRC
    assert 'movs r3, #0x40' in WORKER_SRC
    # M6/M6M deliberately uses the byte-exact v0.16 word-program primitive.
    # M36's E8 engine is separate and is audited by the backend-profile tests.
    erase=re.search(r'sramStackEraseBlock:(.*?)(?=sramStackEraseBlockEnd:)', WORKER_SRC, re.S)
    assert erase
    assert 'push {r4-r7, lr}' in erase.group(1) and 'pop {r4-r7, pc}' in erase.group(1)
    assert 'pop {r3-r7, pc}' not in erase.group(1)
    print('PASS Phase16 lazy restore is SRAM-API-owned; pre-save IRQ only chains')

def worker_binary_audit():
    elf=ROOT/'build_runtime/runtime.elf'
    nm=subprocess.check_output(['nm','-n',str(elf)],text=True)
    def sym(name):
        m=re.search(rf'^([0-9a-fA-F]+)\s+\S\s+{re.escape(name)}$',nm,re.M); assert m,name; return int(m.group(1),16)&~1
    p0,p1=sym('sramStackProgramChunk'),sym('sramStackProgramChunkEnd')
    e0,e1=sym('sramStackEraseBlock'),sym('sramStackEraseBlockEnd')
    assert p1-p0 == 0x86, hex(p1-p0)
    assert e1-e0 == 0x56, hex(e1-e0)
    rel=subprocess.check_output(['readelf','-r',str(elf)],text=True)
    for off in re.findall(r'^([0-9A-Fa-f]{8})\s',rel,re.M):
        x=int(off,16)
        assert not (p0 <= x < p1 or e0 <= x < e1), (hex(x),hex(p0),hex(p1),hex(e0),hex(e1))
    for lo,hi in ((p0,p1),(e0,e1)):
        dis=subprocess.check_output(['/usr/local/swift/usr/bin/llvm-objdump','-d',f'--start-address={lo}',f'--stop-address={hi}',str(elf)],text=True)
        assert re.search(r'\bblx?\b',dis) is None
        assert re.search(r'ldr\s+r\d+, \[pc',dis) is None
    # The assembly wrappers copy only one primitive at a time. The program
    # path reserves 0x88 bytes, erase 0x58; C++ no longer owns a 0x100-byte
    # executable local.
    assert 'sub sp, #0x88' in IRQ_SRC and 'sub sp, #0x58' in IRQ_SRC
    assert 'workerBytes[' not in RUNTIME_SRC
    print('PASS Phase16 stack NOR primitives: program=134B erase=86B, individually copied, PIC/no relocations')


def stack_budget_audit():
    # Compile the C++ unit with Clang's static stack-usage report. The deepest
    # owned-snapshot program call is:
    #   owned IRQ System-LR save 4
    # + gbashOwnedSramHotkeyCommit 48
    # + commitOwnedSramSnapshot 112
    # + M6 stack wrapper push 20 + code buffer 0x88
    # + exact v0.16 copied program worker push 20
    # = 340 bytes beyond the interrupted System SP.
    clang='/usr/local/swift/usr/bin/clang++'
    with tempfile.TemporaryDirectory() as td:
        obj=pathlib.Path(td)/'runtime.o'
        cmd=[clang,'--target=armv4t-none-eabi','-mcpu=arm7tdmi','-mthumb','-std=c++17','-Os',
             '-ffreestanding','-fno-builtin','-fno-exceptions','-fno-rtti','-fno-unwind-tables',
             '-fno-asynchronous-unwind-tables','-fno-stack-protector','-fno-pic','-fno-pie',
             '-fstack-usage','-c',str(ROOT/'runtime/gba_runtime.cpp'),'-o',str(obj)]
        subprocess.check_call(cmd,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        su=pathlib.Path(td)/'runtime.su'
        text=su.read_text()
    def usage(symbol):
        m=re.search(rf':{re.escape(symbol)}\t(\d+)\tstatic$',text,re.M); assert m,symbol; return int(m.group(1))
    owned=usage('gbashOwnedSramHotkeyCommit')
    txn=usage('_ZL23commitOwnedSramSnapshotv')
    assert owned == 48 and txn == 112, (owned,txn)
    deepest=4 + owned + txn + 20 + 0x88 + 20
    assert deepest == 340 and deepest <= 0x160
    # Hardware-proven game-owned IRQ ABI uses no synthetic IRQ-stack frame.
    # The long transaction switches to System mode and saves only System LR,
    # exactly as the v0.16 path that passed WL4 hardware.
    m=re.search(r'gbashSramOwnedShadowHotkeyIrqEntry:(.*?).size gbashSramOwnedShadowHotkeyIrqEntry',IRQ_SRC,re.S)
    assert m and 'GBASH_IRQ_FRAME_ENTER' not in m.group(1) and 'stmdb sp!, {lr}' in m.group(1) and 'r0-r12' not in m.group(1)
    print(f'PASS Phase16 owned hotkey stack budget: System={deepest} bytes (<=0x160), v0.16 IRQ ABI has no added IRQ frame')

def real_rom_audit():
    paths=[('MZM',pathlib.Path('/mnt/data/rom_inputs/MZM.gba'),0x02038000),('WL4',pathlib.Path('/mnt/data/rom_inputs/WL4.gba'),0x02038000)]
    available=[item for item in paths if item[1].exists()]
    if not available:
        print('SKIP Phase16 exact MZM/WL4 scan: target ROM fixtures unavailable')
    for name,p,base in available:
        scan=subprocess.check_output([str(BIN),'scan',str(p),'--save-type','sram'],text=True)
        assert f'SRAM game-owned EWRAM mirror: 0x{base:08X}' in scan
        with tempfile.TemporaryDirectory() as td:
            out=pathlib.Path(td)/f'{name}.gba'
            report=subprocess.check_output([str(BIN),'patch',str(p),str(out),'--nor-profile','m6m','--save-type','sram'],text=True)
            assert f'SRAM shadow:           0x{base:08X} GAME_OWNED_PROVEN' in report
            assert len(out.read_bytes())==0x800000
        print(f'PASS Phase16 {name}: unique game-owned SRAM mirror {base:#010x}')

    # These ambiguity/noise mutations are exact MZM structural regressions.
    mzm=paths[0][1]
    if mzm.exists():
        src=bytearray(mzm.read_bytes())
        one=bytearray(src)
        assert int.from_bytes(one[0xF44:0xF48],'little') == 0x0203F800
        one[0xF44:0xF48]=(0x0203F804).to_bytes(4,'little')
        with tempfile.TemporaryDirectory() as td:
            probe=pathlib.Path(td)/'mzm_weakened_literal.gba'; probe.write_bytes(one)
            scan=subprocess.check_output([str(BIN),'scan',str(probe),'--save-type','sram'],text=True)
            assert 'SRAM game-owned EWRAM mirror:' not in scan

        amb=bytearray(src); block=bytes(src[0xF20:0xF50]); dst=0x760D40
        assert all(x==0xFF for x in amb[dst:dst+len(block)])
        amb[dst:dst+len(block)]=block
        amb[dst+0x0C:dst+0x10]=(0x02017F70).to_bytes(4,'little')
        amb[dst+0x24:dst+0x28]=(0x02017800).to_bytes(4,'little')
        with tempfile.TemporaryDirectory() as td:
            probe=pathlib.Path(td)/'mzm_pair_noise.gba'; probe.write_bytes(amb)
            scan=subprocess.check_output([str(BIN),'scan',str(probe),'--save-type','sram'],text=True)
            assert 'SRAM game-owned EWRAM mirror:' not in scan
        print('PASS Phase16 mirror inference: two-independent-offset proof required; weakened/ambiguous evidence fails closed')
    else:
        print('SKIP Phase16 exact MZM ambiguity/noise mutations: fixture unavailable')

    # Protect the hardware-proven tail-fit path directly when the exact F-Zero
    # fixture is available. This is stronger than manufacturing a truncated
    # unrelated ROM and keeps ROM/NOR placement independent of RAM ownership.
    fzero=pathlib.Path('/mnt/data/FZERO(1).gba')
    if fzero.exists():
        with tempfile.TemporaryDirectory() as td:
            out=pathlib.Path(td)/'fzero.gba'
            report=subprocess.check_output([str(BIN),'patch',str(fzero),str(out),'--nor-profile','m6m','--save-type','sram'],text=True)
            assert 'SRAM shadow:           0x02027000 PRIVATE_COMPATIBILITY' in report
            assert 'block 0 -> 0x410000..0x41FFFF APPENDED_TAIL' in report
            assert 'block 1 -> 0x420000..0x42FFFF APPENDED_TAIL' in report
            assert len(out.read_bytes()) == 0x430000
        print('PASS Phase16 tail-fit regression: exact F-Zero private RAM shadow + 0x410000/0x420000 tail retention')
    else:
        print('SKIP Phase16 exact F-Zero tail-fit regression: fixture unavailable')

def main():
    power_cut_model(); static_runtime_audit(); worker_binary_audit(); stack_budget_audit(); real_rom_audit()
if __name__=='__main__': main()
