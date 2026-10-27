"""Package own app and executable; no Signify or LG binaries are included."""
import gzip, io, json, tarfile
from pathlib import Path
from sync_version import synchronize
synchronize(check=True)
ROOT=Path(__file__).resolve().parent
ID='org.webosbrew.histogramhyperion'
VERSION=json.loads((ROOT/'app/appinfo.json').read_text(encoding='utf-8'))['version']
def tar_bytes(entries):
    stream=io.BytesIO()
    with tarfile.open(fileobj=stream,mode='w') as tar:
        for name,body,mode in entries:
            info=tarfile.TarInfo(name);info.size=len(body);info.mode=mode;info.mtime=0
            tar.addfile(info,io.BytesIO(body))
    return gzip.compress(stream.getvalue(), mtime=0)
entries=[]
for directory,target in [('app','applications/'+ID),('service','services/'+ID+'.service'),('package','packages/'+ID)]:
    for path in sorted((ROOT/directory).iterdir()):
        if path.is_file() and path.name not in {"led-mapping.txt", "led-interpolation.txt"}:entries.append(('usr/palm/'+target+'/'+path.name,path.read_bytes(),0o755 if path.name in {'histogram-service','boot.sh'} else 0o644))
control=f'Package: {ID}\nVersion: {VERSION}\nArchitecture: arm\nMaintainer: Local development\nDescription: HyperDRM ambient light for webOS and Hyperion\nSection: misc\nPriority: optional\n\n'.encode()
members=[('debian-binary',b'2.0\n'),('control.tar.gz',tar_bytes([('control',control,0o644)])),('data.tar.gz',tar_bytes(entries))]
out=bytearray(b'!<arch>\n')
for name,body in members:
    out.extend(f'{name+"/":<16}{0:<12}{0:<6}{0:<6}{"100644":<8}{len(body):<10}`\n'.encode());out.extend(body)
    if len(body)%2:out.extend(b'\n')
destination=ROOT/('HyperDRM_'+VERSION+'_arm.ipk');destination.write_bytes(out)
print(destination)
