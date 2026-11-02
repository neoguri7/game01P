"""Presses the periodic ground into the contract value range palette entry by palette entry, so every colour stays a flat step."""
import numpy as np, os, sys
from PIL import Image
T=np.array(Image.open('env/raw/ground_T2.png').convert('RGB'))
pal,inv=np.unique(T.reshape(-1,3),axis=0,return_inverse=True)
LEVELS=[0.098,0.128,0.150,0.170,0.190,0.205]   # discrete value steps
v0=pal.max(axis=1)/255
lo,hi=np.percentile(T.max(axis=2)/255,[5,95])
out=[]
for c,v in zip(pal,v0):
    t=np.clip(v/hi,0,1)
    target=0.098+t*(0.205-0.098)
    target=min(LEVELS,key=lambda l:abs(l-target))
    s=(c/c.max()) if c.max()>0 else np.ones(3)   # keep hue and saturation
    if target<0.13:                             # lifted joints must not turn red
        g=s.mean(); s=g+(s-g)*0.25
    s=s*np.array([0.95,0.985,1.05]); s=s/s.max()   # a touch cooler than the walls
    out.append(np.round(s*target*255))
out=np.array(out,dtype=np.uint8)
G=out[inv].reshape(T.shape)
v=G.max(axis=2)/255
print('V pct',np.percentile(v,[1,5,25,50,75,95,99]).round(3),'spread %.3f'%(np.percentile(v,95)-np.percentile(v,5)),'mean %.3f'%v.mean(),'colors',len(np.unique(G.reshape(-1,3),axis=0)))
print('periodic iso',(G==np.roll(G,(128,256),axis=(0,1))).all(),'rect wrap diff',np.abs(G[:,0].astype(int)-G[:,-1]).mean().round(2),np.abs(G[0].astype(int)-G[-1]).mean().round(2),'inner',np.abs(G[:,100].astype(int)-G[:,101]).mean().round(2))
Image.fromarray(G).save('env/raw/ground_final.png')
t='D:/repos/game01P/assets/art/env/ground_a.tmp.png'; Image.fromarray(G).save(t); os.replace(t,'D:/repos/game01P/assets/art/env/ground_a.png')
