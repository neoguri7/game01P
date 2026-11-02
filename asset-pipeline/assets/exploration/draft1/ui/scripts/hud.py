"""HUD pieces rebuilt from the finished candidates by moving and tiling whole pixels (never scaling) so each fills its contract canvas."""
import numpy as np, os, sys
from PIL import Image
OUT='D:/repos/game01P/assets/art/ui/'
def load(n): return np.array(Image.open('ui/raw/'+n+'.png').convert('RGBA'))
def save(a,dest):
    a=a.copy(); a[...,3]=np.where(a[...,3]>=128,255,0); a[a[...,3]==0]=0
    t=OUT+dest+'.tmp'; Image.fromarray(a,'RGBA').save(t,format='PNG'); os.replace(t,OUT+dest)
    op=a[a[...,3]==255][:,:3]/255; print(dest,a.shape[1],a.shape[0],Image.fromarray(a).getbbox(),'Vmean %.3f p95 %.3f'%(op.max(axis=1).mean(),np.percentile(op.max(axis=1),95)))
def cyc(lo,hi,n):
    """n indices cycling through lo..hi-1 back and forth, so a tiled rail never shows a hard jump."""
    span=list(range(lo,hi))+list(range(hi-2,lo,-1)); return [span[i%len(span)] for i in range(n)]
def nine(src,box,cx,cy,size,inner=None,fill=None,mx=(0,0),my=(0,0)):
    """box: outer frame in src. cx/cy: corner sizes. size: output canvas. mx,my: transparent margins of the output."""
    x0,y0,x1,y1=box; W,H=size; tw,th=W-mx[0]-mx[1],H-my[0]-my[1]
    xs=list(range(x0,x0+cx))+cyc(x0+cx,x1-cx,tw-2*cx)+list(range(x1-cx,x1))
    ys=list(range(y0,y0+cy))+cyc(y0+cy,y1-cy,th-2*cy)+list(range(y1-cy,y1))
    body=src[np.ix_(ys,xs)]
    out=np.zeros((H,W,4),np.uint8); out[my[0]:my[0]+th,mx[0]:mx[0]+tw]=body
    if inner is not None:
        i=inner; out[my[0]+i[1]:H-my[1]-i[3],mx[0]+i[0]:W-mx[1]-i[2]]=fill
    return out
INK=(17,17,20,255)
def gain(a,g):
    b=a.copy(); b[...,:3]=np.round(b[...,:3]*g).astype(np.uint8); return b
# hud_bar: t_hotbar_b is cut off on the left, so its right half is mirrored; the bands move apart for a 52px channel
a=load('t_hotbar_b'); m=a.copy(); m[:,:210]=a[:,210:][:,::-1]
bar=nine(m,(14,14,406,71),26,17,(420,84),inner=(10,16,10,16),fill=INK,mx=(14,14),my=(1,2))
save(gain(bar,0.9),'hud_bar.png')
# hud_panel: t_panel_pixen, frame to the canvas edge, 24px corners for the nine-slice, flat interior instead of grey noise
p=load('t_panel_pixen')
pan=nine(p,(5,6,238,122),24,24,(256,128),inner=(20,20,20,20),fill=INK)
save(gain(pan,0.72),'hud_panel.png')
# hud_portrait: see-through centre
q=load('portrait'); Q=q.copy()
ys=np.arange(72); 
rgb=q[...,:3].astype(int); v=rgb.max(axis=2)
from collections import deque
inner=np.zeros((72,72),bool); inner[15:57,15:57]=True
for y,x in ((15,15),(15,56),(56,15),(56,56)): inner[y,x]=False
Q[inner]=0
save(Q,'hud_portrait.png')
# hud_panel, second build: the same iron band and gold line as hud_bar, so the floating pieces are one family
pan2=nine(m,(14,14,406,71),24,24,(256,128),inner=(12,16,12,16),fill=INK)
top=pan2[:13,24:232]                     # top band turned 90 degrees makes the side rails, so they stop repeating four rows
side=np.transpose(top,(1,0,2))[:80]
pan2[24:104,0:13]=side
pan2[24:104,243:256]=side[:,::-1]
save(gain(pan2,0.9),'hud_panel.png')
# hud_button: t_button sits small in its canvas; rails tiled out to the contract size
b=load('t_button')
but=nine(b,(47,10,122,46),13,11,(136,56),inner=(9,8,9,8),fill=(30,33,38,255),mx=(1,1),my=(1,1))
save(gain(but,0.85),'hud_button.png')
# hud_turnbar: the raw strip is two pieces with a gap; the whole right piece gives both caps, its channel is tiled to length
tb=load('turnbar')
al=tb[...,3].any(axis=0); cols=np.where(al)[0]; gaps=np.where(np.diff(cols)>1)[0]; print('turnbar pieces',cols[0],cols[gaps],cols[gaps+1],cols[-1])
x0=int(cols[gaps[-1]+1]); x1=int(cols[-1])+1
rows=np.where(tb[:,x0:x1,3].any(axis=1))[0]; y0,y1=int(rows[0]),int(rows[-1])+1
print('piece',x0,x1,y0,y1)
tbar=nine(tb,(x0,y0,x1,y1),16,6,(320,32),mx=(1,1),my=(y0,32-y1))
save(gain(tbar,0.95),'hud_turnbar.png')
