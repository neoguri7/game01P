"""Second pass of the standing objects: keeps the object value below the units (body only, light sources untouched)."""
import numpy as np, os, colorsys
from PIL import Image
ART='D:/repos/game01P/assets/art/env/'
PLAN={'wall_a':('wall1_2',8),'wall_b':('wall1_3',9),'wall_c':('wall1_1',8),'arch':('arch1_0',4),'cliff':('cliff_0',9),
 'pillar':('pillar_7',15),'brazier':('brazier_7',8),'crystal':('crystal_0',5),'tree_dead':('tree_0',14)}
GAIN=0.86
for dest,(raw,dy) in PLAN.items():
    a=np.array(Image.open('env/raw/'+raw+'.png').convert('RGBA')).astype(np.float64)
    rgb=a[...,:3]; v=rgb.max(axis=2); mn=rgb.min(axis=2); s=np.where(v>0,(v-mn)/np.maximum(v,1),0)
    # light sources: saturated warm flame or cyan crystal stay as drawn
    r,g,b=rgb[...,0],rgb[...,1],rgb[...,2]
    glow=((s>0.55)&(r>g)&(v>120))|((b>r*1.25)&(g>r*1.25)&(v>60))
    body=(a[...,3]==255)&~glow
    gain=1.0 if dest=='tree_dead' else GAIN
    rgb[body]=np.round(rgb[body]*gain)
    a=a.astype(np.uint8)
    h=a.shape[0]; out=np.zeros_like(a); out[dy:]=a[:h-dy]; out[out[...,3]==0]=0
    t=ART+dest+'.tmp.png'; Image.fromarray(out,'RGBA').save(t); os.replace(t,ART+dest+'.png')
    op=out[out[...,3]==255][:,:3]/255; vv=op.max(axis=1)
    print(dest,'Vmean %.3f p95 %.3f colors %d'%(vv.mean(),np.percentile(vv,95),len(np.unique(op,axis=0))))
