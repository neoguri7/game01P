#!/usr/bin/env python3
"""후보와 앵커를 한 장에 나란히 놓아 눈으로 비교하기 위한 컨택트 시트를 만든다.

사용법:
  python scripts/contact_sheet.py OUT.png IMG1.png IMG2.png ... [--cell 192] [--cols 4]
"""
import argparse
from pathlib import Path

from PIL import Image, ImageDraw


def checker(size, block=8):
    img = Image.new("RGB", size, (200, 200, 200))
    draw = ImageDraw.Draw(img)
    for y in range(0, size[1], block):
        for x in range(0, size[0], block):
            if (x // block + y // block) % 2:
                draw.rectangle([x, y, x + block - 1, y + block - 1], fill=(160, 160, 160))
    return img


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("out")
    parser.add_argument("images", nargs="+")
    parser.add_argument("--cell", type=int, default=192)
    parser.add_argument("--cols", type=int, default=4)
    args = parser.parse_args()

    label_h = 16
    cols = min(args.cols, len(args.images))
    rows = (len(args.images) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * args.cell, rows * (args.cell + label_h)), (40, 40, 40))
    draw = ImageDraw.Draw(sheet)

    for i, path in enumerate(args.images):
        img = Image.open(path).convert("RGBA")
        # 픽셀아트가 뭉개지지 않도록 정수배 확대 + 최근접 보간만 사용
        scale = max(1, min(args.cell // img.width, args.cell // img.height))
        img = img.resize((img.width * scale, img.height * scale), Image.NEAREST)
        if img.width > args.cell or img.height > args.cell:
            img.thumbnail((args.cell, args.cell), Image.NEAREST)
        cell = checker((args.cell, args.cell))
        cell.paste(img, ((args.cell - img.width) // 2, (args.cell - img.height) // 2), img)
        x, y = (i % cols) * args.cell, (i // cols) * (args.cell + label_h)
        sheet.paste(cell, (x, y + label_h))
        draw.text((x + 3, y + 2), Path(path).name[:28], fill=(255, 255, 255))

    Path(args.out).parent.mkdir(parents=True, exist_ok=True)
    sheet.save(args.out)
    print(args.out)


if __name__ == "__main__":
    main()
