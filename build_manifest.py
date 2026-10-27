"""Describe the exact built IPK for Homebrew Channel."""
import hashlib, json
from pathlib import Path
from sync_version import synchronize
synchronize(check=True)
ROOT=Path(__file__).resolve().parent
BASE='https://gitea.adnau.de/matze19999/HyperDRM'
RAW=BASE+'/raw/branch/main/'
app=json.loads((ROOT/'app/appinfo.json').read_text(encoding='utf-8'))
ipk=ROOT/('HyperDRM_'+app['version']+'_arm.ipk')
data=ipk.read_bytes()
manifest={key:app[key] for key in ('id','version','type','title','appDescription')}
manifest.update(iconUri=RAW+'app/icon.png',sourceUrl=BASE,rootRequired=True,
                ipkUrl=RAW+ipk.name,ipkHash={'sha256':hashlib.sha256(data).hexdigest()},ipkSize=len(data))
(ROOT/'HyperDRM.manifest.json').write_text(json.dumps(manifest,indent=2,ensure_ascii=False)+'\n',encoding='utf-8')
print('Manifest:',ipk.name,len(data),'bytes',manifest['ipkHash']['sha256'])
