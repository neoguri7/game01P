"""Puts chosen candidates on the contract canvas by shifting whole pixels only (no resize), writing via os.replace."""
import numpy as np, os, json, colorsys
from PIL import Image
ART='D:/repos/game01P/assets/art/env/'; RAW='env/raw/'
PLAN={ # dest: (raw, dy)
 'wall_a':('wall1_2',8),'wall_b':('wall1_3',9),'wall_c':('wall1_1',8),'arch':('arch1_0',4),'cliff':('cliff_0',9),
 'pillar':('pillar_7',15),'brazier':('brazier_7',8),'crystal':('crystal_0',5),'tree_dead':('tree_0',14)}
for dest,(raw,dy) in PLAN.items():
    im=Image.open(RAW+raw+'.png').convert('RGBA')
    a=np.array(im)
    assert set(np.unique(a[...,3]))<={0,255}, raw
    b=np.zeros_like(a)
    assert a[a.shape[0]-dy:,:,3].max()==0, raw+' would lose pixels'
    b[dy:]=a[:a.shape[0]-dy]
    b[b[...,3]==0]=0
    t=ART+dest+'.tmp.png'; Image.fromarray(b,'RGBA').save(t); os.replace(t,ART+dest+'.png')
    op=b[b[...,3]==255][:,:3]/255
    v=op.max(axis=1); print(dest,raw,'bbox',Image.fromarray(b).getbbox(),'Vmean %.3f p95 %.3f'%(v.mean(),np.percentile(v,95)))
    # base line of the mass: lowest opaque y per column, for wall-like pieces
    if dest.startswith(('wall','cliff')):
        al=b[...,3]>0; xs=[x for x in range(b.shape[1]) if al[:,x].any()]
        low=np.array([np.where(al[:,x])[0].max() for x in xs]); p=np.polyfit(xs,low,1)
        print('   base fit y=%.2fx+%.1f  at x=64 y=%.1f (anchor y=120)'%(p[0],p[1],np.polyval(p,64)))
for u in 'warrior archer mage healer guardian imp imp_archer imp_shaman'.split():
    a=np.array(Image.open('D:/repos/game01P/assets/art/'+u+'.png').convert('RGBA')); op=a[a[...,3]==255][:,:3]/255; v=op.max(axis=1)
    print(u,'Vmean %.3f p95 %.3f'%(v.mean(),np.percentile(v,95)))
