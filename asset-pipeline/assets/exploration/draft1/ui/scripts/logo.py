"""title_logo: the finished plaque with the game's name set in Cinzel (the game's own OFL font), no anti-aliasing, three flat ember steps."""
import numpy as np, os
from PIL import Image, ImageDraw, ImageFont
OUT='D:/repos/game01P/assets/art/ui/title_logo.png'
FONT='D:/repos/game01P/assets/fonts/Cinzel.ttf'
p=Image.open('ui/raw/logo.png').convert('RGBA')
a=np.array(p)
# interior: rows and columns of the dark plaque
v=a[...,:3].max(axis=2)
print('row230',(v[:,230]).tolist()[:50]); print('col mid',(v[70,80:140]).tolist())
def font(sz,wght=900):
    f=ImageFont.truetype(FONT,sz)
    try: f.set_variation_by_axes([wght])
    except Exception as e: print('no axes',e)
    return f
def mask(text,sz,track):
    f=font(sz); W=sum(int(f.getlength(c))+track for c in text)+4
    m=Image.new('1',(W,sz+12),0); d=ImageDraw.Draw(m); d.fontmode='1'
    x=2
    for c in text:
        d.text((x,2),c,fill=1,font=f); x+=int(f.getlength(c))+track
    b=np.array(m); ys,xs=np.where(b); return b[ys.min():ys.max()+1,xs.min():xs.max()+1]
def stamp(canvas,m,cx,top):
    h,w=m.shape; x0=cx-w//2; y0=top
    H,W=canvas.shape[:2]
    M=np.zeros((H,W),bool); M[y0:y0+h,x0:x0+w]=m
    # dark edge
    E=M.copy()
    for dy in (-1,0,1,2):
        for dx in (-1,0,1):
            E|=np.roll(np.roll(M,dy,0),dx,1)
    canvas[E&~M]=(14,9,7,255)
    for y in range(y0,y0+h):
        t=(y-y0)/max(1,h-1)
        c=(255,214,140,255) if t<0.22 else (236,146,62,255) if t<0.66 else (178,80,32,255)
        canvas[y][M[y]]=c
    return w,h
c=a.copy()
wE,hE=stamp(c,mask('EMBER',40,3),240,36)
wD,hD=stamp(c,mask('DUNGEON',20,5),240,36+hE+6)
print('EMBER',wE,hE,'DUNGEON',wD,hD)
c[c[...,3]==0]=0
t=OUT+'.tmp'; Image.fromarray(c,'RGBA').save(t,format='PNG'); os.replace(t,OUT)
