"""Fold ground512 into one iso-lattice cell (period (256,128),(256,-128)) and cut the inpaint windows."""
import numpy as np, sys
from PIL import Image
I=np.array(Image.open('env/raw/ground512.png').convert('RGB'))
H,W,_=I.shape
print('V pct', np.percentile(I.max(axis=2)/255,[1,5,25,50,75,95,99]).round(3), 'colors',len(np.unique(I.reshape(-1,3),axis=0)))
# seam quality of raw on rect lattice
print('rect wrap diff x',np.abs(I[:,0].astype(int)-I[:,-1]).mean().round(1),' y',np.abs(I[0].astype(int)-I[-1]).mean().round(1),' inner',np.abs(I[:,100].astype(int)-I[:,101]).mean().round(1))
yy,xx=np.mgrid[0:H,0:W]
# diamond membership: |x-255.5|/256+|y-127.5|/128 <= 1 ; outside -> shift by lattice vector into it
T=np.zeros_like(I)
ax=(xx-255.5); ay=(yy-127.5)
inside=np.abs(ax)/256+np.abs(ay)/128<=1
T[inside]=I[inside]
for dx,dy in ((256,128),(-256,128),(256,-128),(-256,-128)):
    sx=xx+dx; sy=yy+dy
    ok=(sx>=0)&(sx<W)&(sy>=0)&(sy<H)&(~inside)
    src=(np.abs(sx-255.5)/256+np.abs(sy-127.5)/128<=1)&ok
    T[src]=I[sy[src],sx[src]]
Image.fromarray(T).save('env/raw/ground_T0.png')
np.save('env/scripts/_lat.npy',np.array([0]))
# window centred on lattice point (256,0): pixel (wx,wy) -> T[(wx+128)%512,(wy-128)%256]
def win(cx,cy,S=256):
    ix=(np.arange(S)+cx-S//2)%W; iy=(np.arange(S)+cy-S//2)%H
    return T[np.ix_(iy,ix)],ix,iy
w,_,_=win(256,0)
Image.fromarray(w).convert('RGB').save('env/raw/ground_win1.png')
# X mask: band of +-13 rows about y-128 = +-(x-128)/2
S=256; yy2,xx2=np.mgrid[0:S,0:S]; d1=np.abs((yy2-127.5)-(xx2-127.5)/2); d2=np.abs((yy2-127.5)+(xx2-127.5)/2)
m=((d1<=13)|(d2<=13)).astype(np.uint8)*255
Image.fromarray(m,'L').save('env/raw/ground_win1_mask.png')
print('mask frac',(m>0).mean().round(3))
