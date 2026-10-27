"""Spatial calibration: red increases left-to-right, green top-to-bottom."""
from pathlib import Path
import subprocess
ROOT=Path(__file__).resolve().parent
import imageio_ffmpeg
w,h=1280,720
pixels=bytearray()
for y in range(h):
    for x in range(w):
        pixels.extend((round(x*255/(w-1)),round(y*255/(h-1)),32))
ppm=ROOT/'calibration-gradient.ppm'
ppm.write_bytes(f'P6\n{w} {h}\n255\n'.encode()+pixels)
video=ROOT/'calibration-gradient.mp4'
subprocess.run([imageio_ffmpeg.get_ffmpeg_exe(),'-y','-loop','1','-framerate','1','-i',str(ppm),'-t','300','-c:v','libx264','-pix_fmt','yuv420p','-r','10','-crf','12','-movflags','+faststart',str(video)],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
print(video)
