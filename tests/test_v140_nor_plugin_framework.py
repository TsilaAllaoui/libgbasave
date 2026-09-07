#!/usr/bin/env python3
"""v1.4 split NOR plug-in framework, wizard/importer, and compiled CLI gates."""
from pathlib import Path
import hashlib, json, subprocess, tempfile

ROOT=Path(__file__).resolve().parents[1]
GEN=ROOT/'generated/nor_profile_db.h'
from test_support import GBASAVEHANDLER_BIN as BIN

def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()

def run(*args, check=True):
    return subprocess.run([str(x) for x in args], cwd=ROOT, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=check)

def main():
    protocols=sorted((ROOT/'config/nor/protocols').glob('*.json'))
    chips=sorted((ROOT/'config/nor/chips').glob('*.json'))
    assert len(protocols)>=4 and len(chips)==5
    assert not list((ROOT/'config/nor').glob('*.json')), 'v1.4 profiles must be split into protocols/ and chips/'

    pmap={json.loads(p.read_text())['key']:json.loads(p.read_text()) for p in protocols}
    cmap={json.loads(p.read_text())['key']:json.loads(p.read_text()) for p in chips}
    assert {'intel-word40','intel-relative-word40','amd-unlock-word'} <= set(pmap)
    e8=[k for k,v in pmap.items() if v.get('driver_enum') in ('IntelE8BufferedRww','IntelStatusRegisterWord10')]
    assert len(e8)==1, f'exactly one Intel E8 RWW protocol required, got {e8}'
    assert cmap['m6m']['protocol_ref']=='intel-word40'
    assert cmap['m36']['protocol_ref']==e8[0]
    assert cmap['m6mgd137']['protocol_ref']=='intel-relative-word40'
    assert cmap['mx26l6420mc-90']['protocol_ref']=='amd-unlock-word'
    for key in ('m6m','m36','m6mgd137','mx26l6420mc-90'):
        assert cmap[key]['qualification']['state']=='hardware-proven'

    before=sha(GEN)
    run('python3','tools/gen_nor_profiles.py','--root',ROOT)
    assert sha(GEN)==before, 'profile generation is not deterministic/in-sync'

    # Recipe compiler must fail closed if a proven driver recipe changes.
    with tempfile.TemporaryDirectory() as td:
        tmp=Path(td)
        (tmp/'config/nor/protocols').mkdir(parents=True)
        (tmp/'config/nor/chips').mkdir(parents=True)
        for p in protocols: (tmp/'config/nor/protocols'/p.name).write_bytes(p.read_bytes())
        for p in chips: (tmp/'config/nor/chips'/p.name).write_bytes(p.read_bytes())
        bad=json.loads((tmp/'config/nor/protocols/intel-word40.json').read_text())
        bad['program_recipe'][0]['value']=0x41
        (tmp/'config/nor/protocols/intel-word40.json').write_text(json.dumps(bad))
        result=run('python3','tools/gen_nor_profiles.py','--root',tmp,check=False)
        assert result.returncode!=0 and 'no longer matches native driver IntelStatusRegister' in result.stdout

    # Wizard: chip using an existing protocol is JSON-only.
    result=run('python3','tools/add_nor_chip.py','--display-name','Example Intel NOR','--key','example-intel',
               '--capacity','0x800000','--protocol','intel-word40','--manufacturer','0x20','--device','0x1234',
               '--dry-run','--non-interactive')
    obj=json.loads(result.stdout)
    assert obj['protocol_ref']=='intel-word40' and obj['capacity_bytes']==0x800000
    assert obj['qualification']['state']=='unqualified'

    # Safe FlashGBX importer recognizes the exact D137 command shape.
    result=run('python3','tools/add_nor_chip.py','--from-flashgbx','tests/fixtures/flashgbx_d137_profile.txt',
               '--key','fixture-d137','--dry-run','--non-interactive')
    # First line is the inference notice; JSON follows.
    payload=result.stdout[result.stdout.index('{'):]
    obj=json.loads(payload)
    assert obj['protocol_ref']=='intel-relative-word40'
    assert obj['capacity_bytes']==0x1000000
    assert obj['reference_ids'][0]=={'offset':0,'manufacturer':0x1C,'device':0xB8}
    assert obj['reference_ids'][1]=={'offset':0x800000,'manufacturer':0x1C,'device':0xB9}

    if BIN.exists():
        out=run(BIN,'nor','list').stdout
        for key in ('m6m','m36','m6mgd137','mx26l6420mc-90'): assert key in out
        out=run(BIN,'nor','show','m6mjd137').stdout
        assert 'm6mgd137' in out and 'hardware-proven' in out and '0x7F0000..0x80FFFF' in out
        assert run(BIN,'nor','validate').stdout.startswith('PASS:')

    print('PASS v1.4 NOR plug-in framework: split schema + strict recipe compiler + wizard + safe FlashGBX importer + CLI introspection')

if __name__=='__main__': main()
