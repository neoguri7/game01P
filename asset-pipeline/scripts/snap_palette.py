#!/usr/bin/env python3
"""AI 출력의 미세한 색 오차를 프로젝트 팔레트로 맞추는 결정론적 보정.

  snap_palette.py IN.png OUT.png [--palette style/palette.json] [--alpha-threshold 128]
                                 [--near-distance 40] [--max-far-fraction 0.10] [--force]

- 알파를 0/255 로 이진화한다 (threshold 이상이면 불투명).
- 모든 불투명 픽셀을 가장 가까운 팔레트 색으로 바꾼다.
- "미세한 오차"인지 판단하는 기준은 바뀐 픽셀 수가 아니라 **팔레트와의 거리**다.
  전 픽셀이 조금씩 어긋난 경우는 보정 대상이지만, 팔레트에서 멀리 떨어진 색이 많으면
  (= 팔레트가 아닌 다른 색 체계로 그린 것) 스냅하면 스타일이 망가지므로 저장하지 않고
  종료 코드 3 으로 끝낸다. 이때는 재생성해야 한다. --force 로 무시할 수 있다.
  거리는 RGB 유클리드 거리 (0~441).
"""
import argparse
import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("src")
    parser.add_argument("dst")
    parser.add_argument("--palette", default="style/palette.json")
    parser.add_argument("--alpha-threshold", type=int, default=128)
    parser.add_argument("--near-distance", type=float, default=40.0,
                        help="이 거리 이내면 미세한 오차로 본다")
    parser.add_argument("--max-far-fraction", type=float, default=0.10,
                        help="near-distance 를 넘는 픽셀이 이 비율을 넘으면 거부")
    parser.add_argument("--force", action="store_true")
    args = parser.parse_args()

    colors = json.loads(Path(args.palette).read_text(encoding="utf-8")).get("colors", [])
    if not colors:
        print(json.dumps({"error": "palette.json 이 비어 있습니다"}, ensure_ascii=False))
        return 2
    pal = np.array([[int(c.lstrip("#")[i:i + 2], 16) for i in (0, 2, 4)] for c in colors], dtype=np.int32)

    arr = np.array(Image.open(args.src).convert("RGBA"))
    opaque = arr[..., 3] >= args.alpha_threshold
    out = np.zeros_like(arr)
    rgb = arr[opaque][:, :3].astype(np.int32)
    if len(rgb) == 0:
        print(json.dumps({"error": "불투명 픽셀이 없습니다"}, ensure_ascii=False))
        return 2
    dist = np.sqrt(((rgb[:, None, :] - pal[None, :, :]) ** 2).sum(axis=2))
    nearest = dist.argmin(axis=1)
    d = dist[np.arange(len(rgb)), nearest]
    out[opaque, :3] = pal[nearest]
    out[opaque, 3] = 255

    far_fraction = float((d > args.near_distance).mean())
    stats = {"opaque_pixels": int(opaque.sum()),
             "changed_fraction": round(float((d > 0).mean()), 4),
             "mean_distance": round(float(d.mean()), 2),
             "p95_distance": round(float(np.percentile(d, 95)), 2),
             "max_distance": round(float(d.max()), 2),
             "far_fraction": round(far_fraction, 4)}

    if far_fraction > args.max_far_fraction and not args.force:
        stats["result"] = (f"거부: 팔레트에서 먼 색이 {far_fraction:.0%} (허용 {args.max_far_fraction:.0%}). "
                           "미세한 오차가 아니라 색 체계가 달라서 스냅하면 스타일이 망가짐. 재생성 필요")
        print(json.dumps(stats, ensure_ascii=False, indent=2))
        return 3

    Path(args.dst).parent.mkdir(parents=True, exist_ok=True)
    Image.fromarray(out).save(args.dst)
    stats["result"] = f"저장: {args.dst}"
    print(json.dumps(stats, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
