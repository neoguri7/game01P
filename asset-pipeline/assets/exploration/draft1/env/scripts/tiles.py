"""tile_* : centre 64x32 of a 64x64 PixelLab iso tile, cut to the exact diamond, stone pixels pulled to the ground's value range."""
import numpy as np, os
from PIL import Image
ART='D:/repos/game01P/assets/art/env/'
def diamond():
    m=np.zeros((32,64),bool)
    for y in range(16):
        w=min(64,2+4*y) if y<15 else 64
        l=32-w//2
        m[y,l:l+w]=True; m[31-y,l:l+w]=True
    return m
M=diamond()
print('diamond rows',M.sum(axis=1)[:4],M.sum(axis=1)[14:18],'bbox full',M.any(axis=0).all(),M.any(axis=1).all())
def tile(raw,dest,stone_gain,floor_v,cool=True):
    a=np.array(Image.open('env/raw/'+raw+'.png').convert('RGBA'))[16:48].astype(float)
    rgb=a[...,:3]; v=rgb.max(axis=2); mn=rgb.min(axis=2); s=(v-mn)/np.maximum(v,1)
    ember=(s>0.45)&(rgb[...,0]>rgb[...,2]*1.5)&(v>70)
    st=~ember
    vv=v[st]/255
    newv=np.minimum(vv*stone_gain,floor_v)
    k=np.ones_like(rgb)
    px=rgb[st]; px=px/np.maximum(px.max(axis=1,keepdims=True),1)
    px=(px+np.array([0.34,0.33,0.33])*0)                      
    grey=px.mean(axis=1,keepdims=True); px=grey+(px-grey)*0.6       # calmer than the raw blue-grey
    px=px*np.array([0.97,0.985,1.03])
    px=px/px.max(axis=1,keepdims=True)*newv[:,None]*255
    rgb[st]=np.round(px)
    out=np.zeros((32,64,4),np.uint8); out[...,:3]=a[...,:3].astype(np.uint8); out[...,3]=255
    # a raw tile may miss a corner pixel of the diamond: fill it from the nearest stone pixel of that row
    alpha=a[...,3]>0
    for y in range(32):
        xs=np.where(alpha[y])[0]
        for x in np.where(M[y]&~alpha[y])[0]:
            out[y,x,:3]=out[y,xs[np.argmin(abs(xs-x))],:3]
    out[~M]=0
    t=ART+dest+'.tmp.png'; Image.fromarray(out,'RGBA').save(t); os.replace(t,ART+dest+'.png')
    op=out[out[...,3]==255][:,:3]/255; print(dest,raw,'Vmean %.3f max %.3f'%(op.max(axis=1).mean(),op.max()),Image.fromarray(out).getbbox(),'colors',len(np.unique(op,axis=0)))
tile('trapset_12','tile_trap',1.0,0.20)
tile('trapset_02','tile_blocked',0.95,0.17)
