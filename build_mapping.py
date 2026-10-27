"""Infer approximate region centers from a measured red/green coordinate ramp."""
import collections,json,statistics
from pathlib import Path
ROOT=Path(__file__).resolve().parent
samples=[json.loads(line) for line in (ROOT/'calibration-samples.jsonl').read_text().splitlines() if line.strip()]
assert len(samples)>=3 and all(s['colorValid'] and len(s['regions'])==16 for s in samples)
regions=[]
for i in range(16):
    color=[statistics.median(s['regions'][i][c] for s in samples) for c in range(3)]
    assert all(max(s['regions'][i][c] for s in samples)-min(s['regions'][i][c] for s in samples)<=4 for c in range(3)), 'Calibration image is changing'
    assert color[2]<50, 'Not the expected red/green calibration ramp'
    regions.append({'id':i,'x':color[0]/255,'y':color[1]/255,'rgb':color})
assert max(r['x'] for r in regions)-min(r['x'] for r in regions)>.7
assert max(r['y'] for r in regions)-min(r['y'] for r in regions)>.7
leds=json.loads((ROOT/'serverinfo.json').read_text())['info']['leds']
mapping=[]
for led in leds:
    x=(led['hmin']+led['hmax'])/2;y=(led['vmin']+led['vmax'])/2
    assert 0<=x<=1 and 0<=y<=1
    mapping.append(min(regions,key=lambda r:(x-r['x'])**2+(y-r['y'])**2)['id'])
(ROOT/'service/led-mapping.txt').write_text(str(len(mapping))+'\n'+' '.join(map(str,mapping))+'\n')
(ROOT/'spatial-calibration.json').write_text(json.dumps({'method':'nearest measured centroid; approximate, without HDR calibration','regions':regions,'ledMapping':mapping},indent=2))
assert len(mapping)>0 and len(set(mapping))>=1
print('LEDs:',len(mapping),'used regions:',dict(sorted(collections.Counter(mapping).items())))
