"""Counts what the eye can miss on a contact sheet, for the frames sources.json keeps (or for given jobs).

    py qa_frames.py                        # every unit clip in sources.json
    py qa_frames.py <label>=<job>:<f,f,..> # given frames

For each frame: columns/rows touching the cell edge that the anchor does not touch (a clipped silhouette), small
detached pixel groups (specks), non-binary alpha, and the bottom row of opaque pixels compared with the anchor
(a sliding or floating foot line).
"""
from __future__ import annotations

import json
import sys
from pathlib import Path

from PIL import Image

GAME = Path(r"D:\repos\game01P")
sys.path.insert(0, str(GAME / "tools" / "anim"))

import build_sheets  # noqa: E402

SPECK = 6  # a detached group of at most this many pixels


def opaque(image: Image.Image) -> list[list[bool]]:
    alpha = image.getchannel("A")
    return [[alpha.getpixel((x, y)) > 0 for x in range(64)] for y in range(64)]


def groups(mask: list[list[bool]]) -> list[int]:
    seen = [[False] * 64 for _ in range(64)]
    sizes = []
    for start_y in range(64):
        for start_x in range(64):
            if not mask[start_y][start_x] or seen[start_y][start_x]:
                continue
            stack, size = [(start_x, start_y)], 0
            seen[start_y][start_x] = True
            while stack:
                x, y = stack.pop()
                size += 1
                for dx in (-1, 0, 1):
                    for dy in (-1, 0, 1):
                        nx, ny = x + dx, y + dy
                        if 0 <= nx < 64 and 0 <= ny < 64 and mask[ny][nx] and not seen[ny][nx]:
                            seen[ny][nx] = True
                            stack.append((nx, ny))
            sizes.append(size)
    return sizes


def edges(mask: list[list[bool]]) -> set[str]:
    found = set()
    if any(mask[0]):
        found.add("top")
    if any(mask[63]):
        found.add("bottom")
    if any(row[0] for row in mask):
        found.add("left")
    if any(row[63] for row in mask):
        found.add("right")
    return found


def foot(mask: list[list[bool]]) -> int:
    return max((y for y in range(64) if any(mask[y])), default=-1)


def report(label: str, job: str, frames: list[int]) -> None:
    unit = label.split(".")[0]
    anchor = opaque(Image.open(GAME / "assets" / "art" / f"{unit}.png").convert("RGBA"))
    anchor_edges, anchor_foot = edges(anchor), foot(anchor)
    notes = []
    for index in frames:
        image = build_sheets.job_frame(job, index)
        mask = opaque(image)
        clipped = edges(mask) - anchor_edges
        sizes = sorted(groups(mask))
        specks = [size for size in sizes[:-1] if size <= SPECK]
        partial = set(image.getchannel("A").tobytes()) - {0, 255}
        parts = []
        if clipped:
            parts.append("clip:" + "/".join(sorted(clipped)))
        if specks:
            parts.append(f"specks:{len(specks)}")
        if partial:
            parts.append(f"alpha:{len(partial)}")
        if abs(foot(mask) - anchor_foot) > 2:
            parts.append(f"foot:{foot(mask) - anchor_foot:+d}")
        if parts:
            notes.append(f"{index}[{' '.join(parts)}]")
    print(f"{label:<22} {' '.join(notes) if notes else 'clean'}")


def main(argv: list[str]) -> int:
    if argv:
        for argument in argv:
            label, _, rest = argument.partition("=")
            job, _, spec = rest.partition(":")
            report(label, job, [int(part) for part in spec.split(",")])
        return 0
    sources = json.loads((GAME / "assets" / "art" / "anim" / "sources.json").read_text(encoding="utf-8"))
    for unit, actions in sources["units"].items():
        for action, entry in actions.items():
            report(f"{unit}.{action}", entry["job"], entry["frames"])
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
