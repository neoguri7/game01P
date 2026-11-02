import numpy as np, sys
from PIL import Image
W,H=512,256
T=np.array(Image.open('env/raw/ground_T0.png').convert('RGB'))
out=np.array(Image.open('env/raw/ground_win1_out.png').convert('RGB'))
print('out size',out.shape,'colors',len(np.unique(out.reshape(-1,3),axis=0)))
m=np.array(Image.open('env/raw/ground_win1_mask.png'))>0
win=T[np.ix_((np.arange(256)+0-128)%H,(np.arange(256)+256-128)%W)]
print('outside mask changed px',(np.abs(out[~m].astype(int)-win[~m]).sum(axis=1)>0).sum())
# paste into T keeping lattice periodicity: every pixel in window updates all its lattice copies
for wy in range(256):
    for wx in range(256):
        if not m[wy,wx]: continue
        x=(wx+256-128); y=(wy-128)
        for (dx,dy) in ((0,0),(256,128)):
            T[(y+dy)%H,(x+dx)%W]=out[wy,wx]
Image.fromarray(T).save('env/raw/ground_T1.png')
# windows for the two seam midpoints; elliptical mask
def win_at(cx,cy,name):
    ix=(np.arange(256)+cx-128)%W; iy=(np.arange(256)+cy-128)%H
    Image.fromarray(T[np.ix_(iy,ix)]).save(f'env/raw/{name}.png')
    yy,xx=np.mgrid[0:256,0:256]
    mk=(((xx-127.5)/44)**2+((yy-127.5)/22)**2<=1).astype(np.uint8)*255
    Image.fromarray(mk,'L').save(f'env/raw/{name}_mask.png')
win_at(128,64,'ground_win2'); win_at(384,64,'ground_win3')
# 2x2 rect repeat and staggered iso check
rep=np.tile(T,(2,2,1)); Image.fromarray(rep).save('env/raw/ground_T1_2x2.png')
print('T periodic', (T==np.roll(T,(128,256),axis=(0,1))).all())
