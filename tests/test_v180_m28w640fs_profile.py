#!/usr/bin/env python3
from pathlib import Path
import json, subprocess, tempfile

ROOT=Path(__file__).resolve().parents[1]

def main():
    subprocess.run(['python3','tools/gen_nor_profiles.py','--root','.'], cwd=ROOT, check=True)
    profile=json.loads((ROOT/'config/nor/chips/m28w640fs-t70za6.json').read_text())
    assert profile['capacity_bytes']==0x800000
    assert profile['protocol_ref']=='intel-word40'
    assert profile['erase_geometry']==[
        {'bytes':0x2000,'count':8},
        {'bytes':0x10000,'count':127},
    ]
    assert profile['allocation_forbidden_ranges'][0]['offset']==0
    assert profile['allocation_forbidden_ranges'][0]['bytes']==0x10000
    assert profile['allocation_forbidden_ranges'][1]['offset']==0x40000
    assert profile['allocation_forbidden_ranges'][1]['bytes']==0x20000

    generated=(ROOT/'generated/nor_profile_db.h').read_text()
    assert 'm28w640fs-t70za6' in generated
    assert 'NorStorageForbiddenRange{0u, 65536u}' in generated
    assert 'NorStorageForbiddenRange{262144u, 131072u}' in generated

    save_patcher=(ROOT/'src/save_patcher.cpp').read_text()
    assert 'targetAllocationForbiddenRanges' in save_patcher
    assert 'effectiveOptions.targetAllocationForbiddenRanges' in save_patcher

    with tempfile.TemporaryDirectory(prefix='gbasave-m28-') as td:
        exe=Path(td)/'m28_constraints'
        subprocess.run([
            'c++','-std=c++17','-O2','-Wall','-Wextra','-Werror',
            '-Iinclude','-Igenerated',
            'tests/m28_target_constraints_host.cpp',
            'src/storage_layout.cpp','src/rom_image.cpp','-o',str(exe)
        ],cwd=ROOT,check=True)
        subprocess.run([str(exe)],cwd=ROOT,check=True)
    print('PASS M28W640FS-T70ZA6 profile: bottom parameter area + NVRP read hole excluded from lib-owned allocation')

if __name__=='__main__':
    main()
