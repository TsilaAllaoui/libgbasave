#!/usr/bin/env python3
from pathlib import Path
import hashlib, subprocess, tempfile
ROOT=Path(__file__).resolve().parents[1]
OLD_STUB_SHA='603fbadc87af89138b2b6913a7ba82d7c3b5a304f5cf075e752fbbfb6436e18d'
COMPACT_SHA='de10e86aea9563b388787fac6b0157ce88a21ccda51801765873f4e9f84a1f29'
def sha(p): return hashlib.sha256(Path(p).read_bytes()).hexdigest()
assert sha(ROOT/'data/direct_rww_assets/sfw_save_stubs.bin')==OLD_STUB_SHA
assert sha(ROOT/'data/direct_rww_assets/flash_compact_rww_payload.bin')==COMPACT_SHA
nor=(ROOT/'src/embedded/nor_patch.cpp').read_text()
exact=(ROOT/'src/embedded/exact_rom_plan.cpp').read_text()
scan=(ROOT/'src/embedded/superfw_savepatch_port.cpp').read_text()
assert 'kStubFlashRead1mDirect' not in nor
assert 'kStubFlashIdent1mBankreset' not in nor
assert 'augment_flash1m_switch_bank' not in exact
assert 'flash_v23_verify_n_sig' in scan
assert 'push_op(p,SFW_OP_FLASH_SWITCH_BANK' not in scan
code=r'''
#include "gbasave/embedded/superfw_savepatch_port.h"
#include "gbasave/embedded/superfw_save_signatures.h"
#include <cstdio>
#include <cstdint>
static void put16(uint8_t*p,uint16_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
template<unsigned N> static void putSig(uint8_t*p,const uint16_t(&s)[N]){for(unsigned i=0;i<N;i++)put16(p+i*2,s[i]);}
int main(){
  SfwSavePlan p{}; sfw_saveplan_init(&p,0x400);
  uint8_t b[0x400]{};
  putSig(b+0x80,flash_v2_verify_sig);
  putSig(b+0x180,flash_v23_verify_n_sig);
  sfw_saveplan_scan_filtered(&p,b,sizeof(b),0,0,SFW_SCAN_FLASH);
  unsigned n=0; for(unsigned i=0;i<p.op_count;i++) if(p.op[i].kind==SFW_OP_FLASH_VERIFY) n++;
  if(n!=2) return 1;
  SfwSavePlan q{}; sfw_saveplan_init(&q,0x1000); q.save_type=SFW_SAVE_FLASH1024K;
  const uint8_t req[]={SFW_OP_FLASH_READ,SFW_OP_FLASH_ERASE_CHIP,SFW_OP_FLASH_ERASE_SECTOR,SFW_OP_FLASH_WRITE_SECTOR};
  for(unsigned i=0;i<4;i++){q.op[q.op_count].kind=req[i];q.op[q.op_count].offset=0x100+i*4;q.op_count++;}
  if(!sfw_saveplan_usable(&q)) return 2;
  std::puts("FLASH1M stream parity host: PASS"); return 0;
}
'''
with tempfile.TemporaryDirectory() as td:
    td=Path(td); src=td/'t.cpp'; exe=td/'t'; src.write_text(code)
    subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror','-I',str(ROOT/'include'),str(src),str(ROOT/'src/embedded/superfw_savepatch_port.cpp'),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
print('PASS dev6 FLASH1M streaming parity: legacy stubs + both verify APIs')
