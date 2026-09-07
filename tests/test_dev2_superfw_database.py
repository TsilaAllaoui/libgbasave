#!/usr/bin/env python3
from pathlib import Path
import subprocess, tempfile, textwrap, hashlib
ROOT=Path(__file__).resolve().parents[1]
DB=ROOT/'data/superfw_patches.db'
GEN=ROOT/'generated/superfw_patch_db.h'
assert DB.exists() and DB.stat().st_size>100000
assert GEN.exists()
src=textwrap.dedent(r'''
#include <cstdio>
#include <cstdint>
#include "gbasave/embedded/superfw_database.h"
int main(){
    const uint8_t ax4e[5]={'A','X','4','E',1};
    SfwSavePlan p{};
    if(!gbasave_superfw_save_plan_lookup(&p,0x400000u,ax4e)) return 2;
    if(p.save_type!=SFW_SAVE_FLASH1024K) return 3;
    if(!p.op_count) return 4;
    if(gbasave_superfw_database_size()<100000u) return 5;
    std::printf("PASS AX4E R1 save=%u ops=%u db=%u\n",p.save_type,p.op_count,gbasave_superfw_database_size());
    return 0;
}
''')
with tempfile.TemporaryDirectory() as td:
    td=Path(td); cpp=td/'t.cpp'; exe=td/'t'; cpp.write_text(src)
    cmd=['c++','-std=c++17','-O2','-Wall','-Wextra','-Werror',f'-I{ROOT}/include',str(cpp),
         str(ROOT/'src/embedded/superfw_savepatch_port.cpp'),str(ROOT/'src/embedded/superfw_database.cpp'),'-o',str(exe)]
    subprocess.run(cmd,check=True)
    out=subprocess.check_output([str(exe)],text=True)
    assert out.startswith('PASS AX4E R1')
print('PASS dev2 lib-owned SuperFW database + AX4E Rev1 lookup')
