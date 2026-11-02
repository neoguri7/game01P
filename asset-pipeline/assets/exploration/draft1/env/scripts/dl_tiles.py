import urllib.request, os, sys
tile_id=sys.argv[1]; prefix=sys.argv[2]; n=int(sys.argv[3]); user=sys.argv[4]
for i in range(n):
    u=f"https://backblaze.pixellab.ai/file/pixellab-tiles/{user}/{tile_id}/tile_{i}.png"
    d=urllib.request.urlopen(urllib.request.Request(u,headers={"User-Agent":"curl/8"}),timeout=60).read()
    o=f"env/raw/{prefix}_{i:02d}.png"; t=o+".tmp"; open(t,"wb").write(d); os.replace(t,o)
print("ok")
