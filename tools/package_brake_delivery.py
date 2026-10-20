#!/usr/bin/env python3
"""Package a verified build and complete reproducible sources, without uploading."""
import argparse
import hashlib
import json
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

root=Path(__file__).resolve().parent.parent
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('archive',type=Path)
parser.add_argument('--output-dir',type=Path,default=root/'build/brake-hero')
args=parser.parse_args();archive=args.archive.resolve();out=args.output_dir.resolve();out.mkdir(parents=True,exist_ok=True)
subprocess.run([sys.executable,str(root/'tools/archive_firmware.py'),'verify',str(archive)],check=True)
subprocess.run([sys.executable,str(root/'tools/verify_brake_embedded_assets.py'),str(archive/'FoloToy-AI-Passport.elf')],check=True)
logs=root/'build/brake-hero/logs'
assert 'Host tests: PASS' in (logs/'bsp-host.log').read_text()
assert 'Vehicle opening/retraction' in (logs/'game-host.log').read_text()
identity=json.loads((archive/'manifest.json').read_text())
sha=identity['full_bin_sha256']
full=archive/'FoloToy-AI-Passport-full.bin'
assert hashlib.sha256(full.read_bytes()).hexdigest()==sha
raw=subprocess.check_output(['git','ls-files','--cached','--others','--exclude-standard','-z'],cwd=root)
paths=sorted(set(p.decode() for p in raw.split(b'\0') if p))
paths=[p for p in paths if (root/p).is_file() and not (root/p).is_symlink()
       and not any(x in Path(p).parts for x in ('.git','.tooling','build','managed_components','firmware'))
       and not p.endswith(('.elf','.map','.bin','.log','.zip'))]
hashes={p:hashlib.sha256((root/p).read_bytes()).hexdigest() for p in paths}
manifest={'application':'Brake Hero','version':'2.0.0','full_bin_sha256':sha,
          'elf_sha256':identity['app_elf_sha256'],'full_bin_bytes':full.stat().st_size,
          'Build':'PASS','Host tests':'PASS','Device tests':'NOT RUN',
          'Repository complete gate':'FAIL: inherited WorldCam README language-pair errors',
          'Unverified':['physical screen/audio/buttons','device FPS/feed gaps/heap/watchdog','NVS restart/power loss'],
          'source_files_sha256':hashes}
mp=out/'Brake-Hero-v2-manifest.json';mp.write_text(json.dumps(manifest,indent=2)+'\n')
shutil.copy2(full,out/'Brake-Hero-v2-full.bin')
(out/'Brake-Hero-v2-full.bin.sha256').write_text(sha+'  Brake-Hero-v2-full.bin\n')
with zipfile.ZipFile(out/'Brake-Hero-v2-source.zip','w',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
    for p in paths:z.write(root/p,'Brake-Hero-v2/'+p)
    z.write(mp,'Brake-Hero-v2/source-build-manifest.json')
with zipfile.ZipFile(out/'Brake-Hero-v2-debug.zip','w',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
    for p in sorted(archive.rglob('*')):
        if p.is_file():z.write(p,p.relative_to(archive))
for name in ['Brake-Hero-v2-source.zip','Brake-Hero-v2-debug.zip']:
    with zipfile.ZipFile(out/name) as z:assert z.testzip() is None
print(json.dumps({'output_dir':str(out),'full_bin_sha256':sha,'elf_sha256':manifest['elf_sha256'],
                  'source_files':len(paths),'source_zip_bytes':(out/'Brake-Hero-v2-source.zip').stat().st_size},indent=2))
