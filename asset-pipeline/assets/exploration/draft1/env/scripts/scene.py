"""Review scene: 2x2 repeat plus staggered lattice of the ground with objects, decals and units at game pixels. Not shipped."""
import sys, numpy as np
from PIL import Image
A='D:/repos/game01P/assets/art/'
g=Image.open('env/raw/ground_final.png').convert('RGBA')
S=Image.new('RGBA',(1024,512))
for x in (0,512):
    for y in (0,256): S.paste(g,(x,y))
def put(name,cx,cy,ax,ay):
    im=Image.open(A+name).convert('RGBA'); S.alpha_composite(im,(cx-ax,cy-ay))
import os
for n,cx,cy,ax,ay in [('env/wall_a.png',200,140,64,120),('env/wall_b.png',312,84,64,120),('env/arch.png',470,110,64,120),('env/cliff.png',80,250,64,120),
    ('env/tree_dead.png',880,180,64,152),('env/pillar.png',640,200,32,88),('env/brazier.png',560,160,32,88),('env/crystal.png',760,260,32,88)]:
    put(n,cx,cy,ax,ay)
for i,u in enumerate('warrior archer mage healer guardian imp imp_archer imp_shaman'.split()):
    put(u+'.png',340+i*70,340+(i%2)*20,32,56)
for n,cx,cy in [('env/decal_moss.png',300,420),('env/decal_rubble.png',470,430),('env/tile_trap.png',620,450),('env/tile_blocked.png',686,450)]:
    im=Image.open(A+n).convert('RGBA')
    if im.getpixel((0,0))[1]<50 or True: S.alpha_composite(im,(cx-im.width//2,cy-im.height//2))
S.convert('RGB').save(sys.argv[1]); print('ok')
