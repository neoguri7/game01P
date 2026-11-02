"""placetest.py out zoom spec... ; spec= raw:ax:ay:dx:dy  -> sprite shifted on its canvas, grid cell diamond drawn at anchor."""
import sys
from PIL import Image, ImageDraw
out=sys.argv[1]; zoom=int(sys.argv[2]); specs=sys.argv[3:]
tiles=[]
for s in specs:
    f,ax,ay,dx,dy=s.split(':'); ax,ay,dx,dy=int(ax),int(ay),int(dx),int(dy)
    im=Image.open(f).convert('RGBA'); w,h=im.size
    pad=32
    c=Image.new('RGBA',(w+2*pad,h+pad),(58,56,54,255))
    d=ImageDraw.Draw(c)
    cx,cy=ax+pad,ay
    d.polygon([(cx,cy-16),(cx+32,cy),(cx,cy+16),(cx-32,cy)],outline=(255,210,60,255))
    c.alpha_composite(im,(pad+dx,dy)) if dy>=0 else c.alpha_composite(im.crop((0,-dy,w,h)),(pad+dx,0))
    d=ImageDraw.Draw(c)
    d.line([(cx,cy-16),(cx+32,cy),(cx,cy+16),(cx-32,cy),(cx,cy-16)],fill=(255,210,60,255))
    d.point((cx,cy),fill=(255,0,0,255))
    d.rectangle([pad,0,pad+w-1,h-1],outline=(120,60,160,255))
    tiles.append((c.resize((c.width*zoom,c.height*zoom),Image.NEAREST),f.split('/')[-1]+f" d{dx},{dy}"))
W=sum(t.width+6 for t,_ in tiles); H=max(t.height for t,_ in tiles)+14
s=Image.new('RGB',(W,H),(0,0,0)); d=ImageDraw.Draw(s); x=0
for t,l in tiles:
    s.paste(t.convert('RGB'),(x,14)); d.text((x+2,1),l,fill=(255,208,96)); x+=t.width+6
s.save(out); print(s.size)
