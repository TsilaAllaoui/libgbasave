#!/usr/bin/env python3
"""NOR protocol coverage: every compiled backend must have a physical target."""
from pathlib import Path
import json, shutil, subprocess, tempfile

ROOT=Path(__file__).resolve().parents[1]
GEN=ROOT/'tools/gen_nor_profiles.py'

def main():
    with tempfile.TemporaryDirectory() as td:
        tmp=Path(td)
        shutil.copytree(ROOT/'config', tmp/'config')
        (tmp/'generated').mkdir()
        # Duplicate the E8 protocol under a second key but do not point a chip at it.
        src=tmp/'config/nor/protocols/intel-e8-rww-128k.json'
        obj=json.loads(src.read_text())
        obj['key']='orphan-e8-test'
        (tmp/'config/nor/protocols/orphan-e8-test.json').write_text(json.dumps(obj,indent=2)+'\n')
        cp=subprocess.run(['python3',str(GEN),'--root',str(tmp)],text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        assert cp.returncode != 0, cp.stdout
        assert 'protocol orphan-e8-test: no chip profile references this protocol' in cp.stdout, cp.stdout
    print('PASS NOR protocol coverage: orphan backend rejected with actionable error')

if __name__=='__main__': main()
