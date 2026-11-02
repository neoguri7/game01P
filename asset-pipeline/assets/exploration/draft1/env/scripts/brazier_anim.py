"""Six-frame flame loop made from the flame pixels of brazier.png itself. Frame 0 is the still picture; the body never moves."""
import numpy as np, os
from PIL import Image
ART='D:/repos/game01P/assets/art/env/'
a=np.array(Image.open(ART+'brazier.png').convert('RGBA'))
H,W=a.shape[:2]
rgb=a[...,:3].astype(int); v=rgb.max(axis=2); mn=rgb.min(axis=2)
yy,xx=np.mgrid[0:H,0:W]
zone=(xx>=25)&(xx<=39)&(yy>=15)&(yy<=34)
warm=(rgb[...,0]>rgb[...,1]+25)&(rgb[...,0]>110)
core=(v>150)                                  # the pale centre of the flame
fl=zone&(a[...,3]==255)&(warm|core)
fl[23:,:]&=(yy[23:,:]<=34)
ys,xs=np.where(fl); print('flame px',fl.sum(),'bbox',xs.min(),ys.min(),xs.max(),ys.max())
sparks=fl&(yy<20)
main=fl&~sparks
# base without flame: above the rim transparent, inside the bowl filled from the coal row next to it
base=a.copy()
base[fl&(yy<23)]=0
for y in range(23,35):
    row=np.where(main[y])[0]
    if len(row)==0: continue
    fill=a[y,row.min()-1].copy() if a[y,row.min()-1,3]==255 else a[y,row.max()+1].copy()
    base[y,row]=fill
BASEY=34; TOP=ys[~sparks[ys,xs]].min()
SP=[(a[y,x].copy(),x,y) for y,x in zip(*np.where(sparks))]
params=[(1,1.12),(2,0.92),(0,1.2),(-2,0.95),(-1,1.08)]
frames=[a.copy()]
for i,(sway,st) in enumerate(params,1):
    f=base.copy()
    flip=i in (2,3)
    for y in range(12,BASEY+1):
        t=(BASEY-y)/max(1,BASEY-TOP)                # 0 at the coals, 1 at the tip
        sy=int(round(BASEY-(BASEY-y)/st))
        if sy<TOP: continue
        dx=int(round(sway*max(0,t)**1.4))
        for x in range(24,41):
            sx=x-dx
            if flip: sx=63-sx
            if 0<=sx<W and main[sy,sx]:
                if a[sy,sx,3]==255: f[y,x]=a[sy,sx]
    for c,x,y in SP:
        ny=y-((i*2)%6); nx=x+(1 if (i+x)%3==0 else -1 if (i+x)%3==1 else 0)
        if ny>=8 and f[ny,nx,3]==0 and (i+y)%4!=0: f[ny,nx]=c
    f[f[...,3]==0]=0
    frames.append(f)
# body check: only the flame zone may differ
for i,f in enumerate(frames):
    d=(f!=frames[0]).any(axis=2); ys2,xs2=np.where(d)
    if i: print(i,'changed',d.sum(),'bbox',xs2.min(),ys2.min(),xs2.max(),ys2.max())
strip=np.concatenate(frames,axis=1)
t=ART+'brazier_anim.tmp.png'; Image.fromarray(strip,'RGBA').save(t); os.replace(t,ART+'brazier_anim.png')
crop=strip[8:44,:]
Image.fromarray(crop,'RGBA').resize((crop.shape[1]*5,crop.shape[0]*5),Image.NEAREST).save('C:/Users/marti/AppData/Local/Temp/art/brz_strip.png')
bg=Image.new('RGBA',(crop.shape[1]*5,crop.shape[0]*5),(58,56,54,255)); bg.alpha_composite(Image.open('C:/Users/marti/AppData/Local/Temp/art/brz_strip.png')); bg.convert('RGB').save('C:/Users/marti/AppData/Local/Temp/art/brz_strip.png')
