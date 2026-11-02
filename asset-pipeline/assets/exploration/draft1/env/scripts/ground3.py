import numpy as np, os
from PIL import Image
W,H=512,256
T=np.array(Image.open('env/raw/ground_T1.png').convert('RGB'))
yy,xx=np.mgrid[0:256,0:256]
mk=(((xx-127.5)/44)**2+((yy-127.5)/22)**2<=1)
for name,(cx,cy) in (('ground_win2_out',(128,64)),('ground_win3_out',(384,64))):
    o=np.array(Image.open(f'env/raw/{name}.png').convert('RGB'))
    for wy,wx in zip(*np.where(mk)):
        x=wx+cx-128; y=wy+cy-128
        for dx,dy in ((0,0),(256,128)):
            T[(y+dy)%H,(x+dx)%W]=o[wy,wx]
print('periodic',(T==np.roll(T,(128,256),axis=(0,1))).all())
Image.fromarray(T).save('env/raw/ground_T2.png')
rgb=T/255; v=rgb.max(axis=2)
print('V pct',np.percentile(v,[5,25,50,75,95]).round(3),'colors',len(np.unique(T.reshape(-1,3),axis=0)))
