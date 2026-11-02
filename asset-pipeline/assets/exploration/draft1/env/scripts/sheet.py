"""sheet.py out.png zoom cols bg files...  : nearest-neighbour contact sheet with index labels. Review only, never shipped."""
import sys
from PIL import Image, ImageDraw
out, zoom, cols, bg = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), sys.argv[4]
files = sys.argv[5:]
ims = [Image.open(f).convert("RGBA") for f in files]
w = max(i.width for i in ims) * zoom + 8
h = max(i.height for i in ims) * zoom + 20
rows = (len(ims) + cols - 1) // cols
c = Image.new("RGBA", (w * cols, h * rows), bg)
d = ImageDraw.Draw(c)
for n, (im, f) in enumerate(zip(ims, files)):
    x, y = (n % cols) * w + 4, (n // cols) * h + 16
    big = im.resize((im.width * zoom, im.height * zoom), Image.NEAREST)
    c.alpha_composite(big, (x, y))
    d.text((x, y - 13), f.replace("\\", "/").split("/")[-1][:-4], fill="#ffd060")
c.convert("RGB").save(out)
print(c.size)
