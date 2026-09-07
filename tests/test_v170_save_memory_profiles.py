#!/usr/bin/env python3
from pathlib import Path
import subprocess, tempfile
ROOT=Path(__file__).resolve().parents[1]

def main():
    subprocess.run(['python3','tools/gen_save_memory_profiles.py','--root','.'],cwd=ROOT,check=True)
    with tempfile.TemporaryDirectory(prefix='gbasave-save-memory-') as td:
        exe=Path(td)/'save_memory_profiles_host'
        subprocess.run(['c++','-std=c++17','-O2','-Wall','-Wextra','-Werror','-Iinclude','-Igenerated',
                        'tests/save_memory_profiles_host.cpp','src/save_memory_profiles.cpp','src/embedded/save_memory_patch.cpp','src/embedded/superfw_savepatch_port.cpp','-o',str(exe)],cwd=ROOT,check=True)
        subprocess.run([str(exe)],cwd=ROOT,check=True)
if __name__=='__main__': main()
