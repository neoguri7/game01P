"""Contact sheets for PixelLab animation jobs, used to look at every frame before it goes into sources.json.

    py contact.py <out.png> <label>=<job>[:<frames>] ...

Each argument is one row: frame cells at 3x on a mid-grey ground, frame numbers on top, frame 0 checked against
the anchor named by the label (`warrior.walk` -> assets/art/warrior.png). Frames go through the same cache as
build_sheets.py, so a later build does not download them again. Rows with a job that is not 64x64 are reported.
"""
from __future__ import annotations

import sys
from pathlib import Path

from PIL import Image, ImageDraw

GAME = Path(r"D:\repos\game01P")
sys.path.insert(0, str(GAME / "tools" / "anim"))

import build_sheets  # noqa: E402

SCALE = 3
CELL = 64 * SCALE
LABEL = 14
GROUND = (72, 80, 88, 255)


def row_frames(job: str, spec: str | None) -> list[int]:
    return [int(part) for part in spec.split(",")] if spec else []


def all_frames(job: str) -> list[int]:
    found = []
    for index in range(0, 41):
        try:
            build_sheets.job_frame(job, index)
        except build_sheets.SourceError:
            break
        found.append(index)
    return found


def main(argv: list[str]) -> int:
    out, rows = Path(argv[0]), []
    for argument in argv[1:]:
        label, _, rest = argument.partition("=")
        job, _, spec = rest.partition(":")
        rows.append((label, job, row_frames(job, spec or None) or all_frames(job)))
    width = max(len(frames) for _, _, frames in rows) * CELL
    height = len(rows) * (CELL + LABEL)
    sheet = Image.new("RGBA", (width, height), GROUND)
    draw = ImageDraw.Draw(sheet)
    for row_index, (label, job, frames) in enumerate(rows):
        top = row_index * (CELL + LABEL)
        unit = label.split(".")[0]
        anchor = build_sheets.visible_pixels(Image.open(GAME / "assets" / "art" / f"{unit}.png"))
        same = build_sheets.visible_pixels(build_sheets.job_frame(job, 0)) == anchor
        print(f"{label:<22} {job} frames={len(frames)} anchor={'ok' if same else 'STALE'}")
        for column, index in enumerate(frames):
            frame = build_sheets.job_frame(job, index)
            if frame.size != (64, 64):
                print(f"  frame {index} is {frame.size}")
            big = frame.resize((CELL, CELL), Image.NEAREST)
            sheet.alpha_composite(big, (column * CELL, top + LABEL))
            draw.text((column * CELL + 2, top + 1), f"{label} {index}" if column == 0 else str(index),
                      fill=(255, 255, 255, 255))
    out.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(out)
    print(f"wrote {out} {sheet.size}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
