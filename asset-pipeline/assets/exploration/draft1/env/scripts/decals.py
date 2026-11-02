"""decal_* : candidate on the 128x64 canvas, value pulled down to sit on the ground."""
import sys, numpy as np, os
from PIL import Image
ART='D:/repos/game01P/assets/art/env/'
raw,dest,gain=sys.argv[1],sys.argv[2],float(sys.argv[3])
a=np.array(Image.open('env/raw/'+raw+'.png').convert('RGBA'))
print(raw,a.shape,'alpha vals',np.unique(a[...,3])[:6],Image.fromarray(a).getbbox())
a[...,3]=np.where(a[...,3]>=128,255,0)
a[...,:3]=np.round(a[...,:3]*gain).astype(np.uint8)
m=(a[...,:3].max(axis=2)<34)&(a[...,3]==255); a[m]=(22,25,28,255)  # outlines stay darker than the ground but not pure black
a[a[...,3]==0]=0
op=a[a[...,3]==255][:,:3]/255; print('Vmean %.3f p95 %.3f'%(op.max(axis=1).mean(),np.percentile(op.max(axis=1),95)))
t=ART+dest+'.tmp.png'; Image.fromarray(a,'RGBA').save(t); os.replace(t,ART+dest+'.png')
