"""Build the deterministic native video-plane calibration stimulus (H.264)."""
import argparse, subprocess
from pathlib import Path

parser=argparse.ArgumentParser()
parser.add_argument('--ffmpeg',default='ffmpeg')
args=parser.parse_args()
root=Path(__file__).resolve().parent
width,height=1920,1080
filters=[]
for step in range(32):
    edge,index=divmod(step,8)
    center=(index+.5)/8
    if edge==0: x,y,w,h=int(center*width-192),0,384,162
    elif edge==1: x,y,w,h=1632,int(center*height-108),288,216
    elif edge==2: x,y,w,h=int((1-center)*width-192),918,384,162
    else: x,y,w,h=0,int((1-center)*height-108),288,216
    left,top=max(0,x),max(0,y)
    w,h=min(width,x+w)-left,min(height,y+h)-top
    start=4+step*2
    filters.append(f"drawbox=x={left}:y={top}:w={w}:h={h}:color=white:t=fill:enable='gte(t,{start})*lt(t,{start+2})'")
# A black gap, a full white reference, then white/black split sweeps. A region's
# white fraction at split s is the fraction of its sampling rectangle left of s,
# so the nine splits recover x0/x1 and y0/y1 of every LG histogram region.
filters.append("drawbox=x=0:y=0:w=1920:h=1080:color=white:t=fill:enable='gte(t,70)*lt(t,72)'")
for step in range(9):
    w=round((step+1)/10*width)
    start=72+step*2
    filters.append(f"drawbox=x=0:y=0:w={w}:h={height}:color=white:t=fill:enable='gte(t,{start})*lt(t,{start+2})'")
for step in range(9):
    h=round((step+1)/10*height)
    start=90+step*2
    filters.append(f"drawbox=x=0:y=0:w={width}:h={h}:color=white:t=fill:enable='gte(t,{start})*lt(t,{start+2})'")
# Four color quadrants for visual validation after measurement.
for x,y,color in [(0,0,'red'),(960,0,'lime'),(960,540,'blue'),(0,540,'yellow')]:
    filters.append(f"drawbox=x={x}:y={y}:w=960:h=540:color={color}:t=fill:enable='gte(t,108)'")
subprocess.run([args.ffmpeg,'-hide_banner','-loglevel','error','-y','-f','lavfi','-i',
               'color=black:s=1920x1080:r=30:d=116','-vf',','.join(filters),
               '-c:v','libx264','-preset','fast','-crf','16','-pix_fmt','yuv420p',
               '-profile:v','main','-level','4.0','-g','30','-an','-movflags','+faststart',
               str(root/'app/calibration.mp4')],check=True)
print(root/'app/calibration.mp4')
