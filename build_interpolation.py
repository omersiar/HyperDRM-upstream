"""Interpolate between calibrated neighboring samples along the screen rim."""
import bisect,json
from pathlib import Path
ROOT=Path(__file__).resolve().parent
cal=json.loads((ROOT/'spatial-calibration.json').read_text())
leds=json.loads((ROOT/'serverinfo.json').read_text())['info']['leds']
def perimeter(x,y):
    edge=min(range(4),key=lambda e:[y,1-x,1-y,x][e])
    return [x,1+y,3-x,4-y][edge]%4
used=set(cal['ledMapping'])
nodes=sorted((perimeter(r['x'],r['y']),r['id']) for r in cal['regions'] if r['id'] in used)
positions=[p for p,_ in nodes]
rows=[]
for led in leds:
    t=perimeter((led['hmin']+led['hmax'])/2,(led['vmin']+led['vmax'])/2)
    right=bisect.bisect_right(positions,t)%len(nodes);left=(right-1)%len(nodes)
    p0,a=nodes[left];p1,b=nodes[right]
    span=(p1-p0)%4;distance=(t-p0)%4
    weight=round(distance/span*65535)
    assert 0<=weight<=65535
    rows.append((a,b,weight))
(ROOT/'service/led-interpolation.txt').write_text(str(len(rows))+'\n'+''.join(f'{a} {b} {w}\n' for a,b,w in rows))
(ROOT/'spatial-interpolation.json').write_text(json.dumps({'method':'linear interpolation along calibrated screen perimeter','nodes':nodes,'weights':rows},indent=2))
assert len(rows)==len(leds)
print(len(rows),'LED blends between',len(nodes),'measured rim regions; wraparound included')
