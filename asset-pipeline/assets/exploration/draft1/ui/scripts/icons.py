"""Chosen icon candidates -> contract paths. Drops the stray marks the style input left in the corners (loose specks, not the sparks that were asked for)."""
import numpy as np, os, json
from PIL import Image
OUT='D:/repos/game01P/assets/art/ui/'
PICK={'strike':('i_strike_12',90),'cleave':('t_cleave',0),'arrow':('i_arrow_12',24),'volley':('i_volley_15',24),'bolt':('i_bolt_9',24),'fog':('i_fog_8',24),
      'heal':('i_heal_5',0),'buff':('i_buff_5',24),'debuff':('i_debuff_18',24),'lunge':('i_lunge_7',24),'snipe':('i_snipe_24',24),'firebolt':('i_firebolt_6',0)}
def comps(m):
    seen=np.zeros(m.shape,bool); out=[]
    for y,x in zip(*np.where(m)):
        if seen[y,x]: continue
        st=[(y,x)]; seen[y,x]=True; c=[]
        while st:
            a,b=st.pop(); c.append((a,b))
            for dy in (-1,0,1):
                for dx in (-1,0,1):
                    Y,X=a+dy,b+dx
                    if 0<=Y<m.shape[0] and 0<=X<m.shape[1] and m[Y,X] and not seen[Y,X]: seen[Y,X]=True; st.append((Y,X))
        out.append(c)
    return sorted(out,key=len,reverse=True)
for k,(raw,minc) in PICK.items():
    a=np.array(Image.open('ui/raw/'+raw+'.png').convert('RGBA'))
    a[...,3]=np.where(a[...,3]>=128,255,0)
    cs=comps(a[...,3]>0); dropped=0
    for c in cs[1:]:
        if len(c)<minc:
            for y,x in c: a[y,x]=0
            dropped+=len(c)
    a[a[...,3]==0]=0
    t=OUT+f'skill_{k}.png.tmp'; Image.fromarray(a,'RGBA').save(t,format='PNG'); os.replace(t,OUT+f'skill_{k}.png')
    print(k,raw,'comps',len(cs),'dropped',dropped,Image.fromarray(a).getbbox())
