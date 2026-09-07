#!/usr/bin/env python3
"""Current NOR schema compatibility: canonical Intel E8 name + legacy field omission."""
from pathlib import Path
import json, subprocess, tempfile

ROOT=Path(__file__).resolve().parents[1]
GEN=ROOT/'tools/gen_nor_profiles.py'

def main():
    with tempfile.TemporaryDirectory() as td:
        tmp=Path(td)
        (tmp/'config/nor/protocols').mkdir(parents=True)
        (tmp/'config/nor/chips').mkdir(parents=True)
        for p in (ROOT/'config/nor/protocols').glob('*.json'):
            (tmp/'config/nor/protocols'/p.name).write_bytes(p.read_bytes())
        for p in (ROOT/'config/nor/chips').glob('*.json'):
            (tmp/'config/nor/chips'/p.name).write_bytes(p.read_bytes())

        # Model the current upstream spelling/schema seen by consumers:
        # IntelE8BufferedRww, renamed protocol key, no legacy direct-engine bool.
        old=tmp/'config/nor/protocols/m36-e8-128k.json'
        if not old.exists():
            candidates=[]
            for q in (tmp/'config/nor/protocols').glob('*.json'):
                o=json.loads(q.read_text())
                if o.get('driver_enum') in ('IntelE8BufferedRww','IntelStatusRegisterWord10'):
                    candidates.append(q)
            assert len(candidates)==1
            old=candidates[0]
        obj=json.loads(old.read_text())
        obj['key']='intel-e8-rww-128k'
        obj['driver_enum']='IntelE8BufferedRww'
        obj.pop('supports_direct_protocol_engine',None)
        new=tmp/'config/nor/protocols/intel-e8-rww-128k.json'
        new.write_text(json.dumps(obj,indent=2)+'\n')
        if old != new: old.unlink()

        m36=tmp/'config/nor/chips/m36.json'
        chip=json.loads(m36.read_text())
        chip['protocol_ref']='intel-e8-rww-128k'
        m36.write_text(json.dumps(chip,indent=2)+'\n')
        (tmp/'generated').mkdir()

        cp=subprocess.run(['python3',str(GEN),'--root',str(tmp)],text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        assert cp.returncode==0,cp.stdout
        out=(tmp/'generated/nor_profile_db.h').read_text()
        assert 'NorFlashType::IntelE8BufferedRww' in out
        assert '"intel-e8-rww-128k"' in out
        # Missing legacy flag must infer to true for the reviewed E8 driver.
        line=out.split('"intel-e8-rww-128k"',1)[1].split('},',1)[0]
        assert 'true, true' in line.replace('\n',' '), line

    print('PASS current NOR schema compatibility: IntelE8BufferedRww + inferred direct protocol engine')

if __name__=='__main__': main()
