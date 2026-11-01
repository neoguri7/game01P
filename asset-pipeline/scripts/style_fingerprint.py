#!/usr/bin/env python3
"""스타일 지문(fingerprint): 승인된 앵커에서 측정 가능한 스타일 특징을 뽑아 후보와 비교한다.

  build:  style_fingerprint.py build ANCHOR.png [ANCHOR.png ...] --out style/fingerprints/character.json
                                     [--frame-size W H]
  check:  style_fingerprint.py check IMAGE --fingerprint style/fingerprints/character.json
                                     [--frame-size W H]

- 앵커는 종류(character, tile, item, ui)별로 따로 지문을 만든다. 종류가 다르면 수치 분포가 다르다.
- 스프라이트 시트는 --frame-size 로 프레임 단위로 잘라 각 프레임을 표본 1개로 센다.
- 판정은 3단계다.
    통과: 앵커 범위 +- margin 안
    경고: margin 의 1~2배 벗어남
    실패: margin 의 2배 초과
  margin = max(앵커 표준편차, 지표별 최소 허용폭)
- 종료 코드: 실패가 하나라도 있으면 1.
"""
import argparse
import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image

WEIGHTS = np.array([0.299, 0.587, 0.114], dtype=np.float32)

# 지표별 최소 허용폭 (앵커가 적어 표준편차가 0 에 가까울 때의 하한)
FLOORS = {
    "n_colors": 2.0,
    "fill_ratio": 0.05,
    "height_ratio": 0.06,
    "mean_luma": 0.06,
    "mean_sat": 0.08,
    "outline_luma": 0.08,
    "outline_top2_share": 0.12,
    "light_dx": 0.06,
    "light_dy": 0.06,
}
# 한쪽으로만 검사하는 지표 (색 수는 앵커보다 많을 때만 문제)
ONE_SIDED_UPPER = {"n_colors"}

MIN_ANCHOR_SAMPLES = 3


def split_frames(arr, frame_size):
    if not frame_size:
        return [arr]
    fw, fh = frame_size
    h, w = arr.shape[:2]
    cols, rows = w // fw, h // fh
    return [arr[r * fh:(r + 1) * fh, c * fw:(c + 1) * fw] for r in range(rows) for c in range(cols)]


def luma_of(rgb_uint8):
    return (rgb_uint8.astype(np.float32) / 255.0) @ WEIGHTS


def measure(frame):
    """프레임 1개의 스타일 지표. 비어 있으면 None."""
    opaque = frame[..., 3] > 0
    n = int(opaque.sum())
    if n == 0:
        return None
    h, w = opaque.shape
    rgb = frame[opaque][:, :3]
    luma = luma_of(rgb)
    rgbf = rgb.astype(np.float32) / 255.0
    mx, mn = rgbf.max(axis=1), rgbf.min(axis=1)
    sat = np.where(mx > 0, (mx - mn) / np.maximum(mx, 1e-6), 0.0)

    ys, xs = np.nonzero(opaque)
    padded = np.pad(opaque, 1)
    inner = padded[:-2, 1:-1] & padded[2:, 1:-1] & padded[1:-1, :-2] & padded[1:-1, 2:]
    boundary = opaque & ~inner
    bpix = frame[boundary][:, :3]
    _, bcounts = np.unique(bpix, axis=0, return_counts=True)

    # 빛의 방향: 실루엣 bbox 를 반으로 나눠 밝기를 비교한다
    y0, y1, x0, x1 = ys.min(), ys.max(), xs.min(), xs.max()
    ymid, xmid = (y0 + y1) / 2.0, (x0 + x1) / 2.0
    lum_img = np.zeros((h, w), np.float32)
    lum_img[opaque] = luma

    def half_mean(mask):
        m = opaque & mask
        return float(lum_img[m].mean()) if m.any() else 0.0

    yy, xx = np.mgrid[0:h, 0:w]
    return {
        "n_colors": float(len(np.unique(rgb, axis=0))),
        "fill_ratio": n / float(h * w),
        "height_ratio": (y1 - y0 + 1) / float(h),
        "mean_luma": float(luma.mean()),
        "mean_sat": float(sat.mean()),
        "outline_luma": float(luma_of(bpix).mean()),
        "outline_top2_share": float(np.sort(bcounts)[::-1][:2].sum() / bcounts.sum()),
        "light_dx": half_mean(xx > xmid) - half_mean(xx <= xmid),
        "light_dy": half_mean(yy > ymid) - half_mean(yy <= ymid),
    }


def samples_from(path, frame_size):
    arr = np.array(Image.open(path).convert("RGBA"))
    out = []
    for f in split_frames(arr, frame_size):
        m = measure(f)
        if m is not None:
            out.append(m)
    return out


def cmd_build(args):
    samples = []
    for p in args.anchors:
        samples.extend(samples_from(p, args.frame_size))
    if len(samples) < MIN_ANCHOR_SAMPLES:
        print(json.dumps({"error": f"표본이 {len(samples)}개뿐입니다. 최소 {MIN_ANCHOR_SAMPLES}개 이상의 앵커(프레임)가 필요합니다."},
                         ensure_ascii=False))
        return 2
    stats = {}
    for name in FLOORS:
        vals = np.array([s[name] for s in samples], dtype=np.float64)
        stats[name] = {"min": float(vals.min()), "max": float(vals.max()),
                       "mean": float(vals.mean()), "std": float(vals.std())}
    out = {"samples": len(samples), "anchors": [str(p) for p in args.anchors],
           "frame_size": args.frame_size, "metrics": stats}
    Path(args.out).parent.mkdir(parents=True, exist_ok=True)
    Path(args.out).write_text(json.dumps(out, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps({"written": args.out, "samples": len(samples),
                      "summary": {k: [round(v["min"], 3), round(v["max"], 3)] for k, v in stats.items()}},
                     ensure_ascii=False, indent=2))
    return 0


def cmd_check(args):
    fp_path = Path(args.fingerprint)
    if not fp_path.exists():
        print(json.dumps({"status": "skipped", "reason": f"지문 파일 없음: {args.fingerprint}"}, ensure_ascii=False))
        return 0
    fp = json.loads(fp_path.read_text(encoding="utf-8"))
    frame_size = args.frame_size or fp.get("frame_size")
    arr = np.array(Image.open(args.image).convert("RGBA"))
    frames = split_frames(arr, frame_size)
    measured = [(i, measure(f)) for i, f in enumerate(frames)]
    measured = [(i, m) for i, m in measured if m is not None]
    if not measured:
        print(json.dumps({"status": "fail", "reason": "불투명 픽셀이 없음"}, ensure_ascii=False))
        return 1

    results, worst_status = [], "pass"
    rank = {"pass": 0, "warn": 1, "fail": 2}
    for name, st in fp["metrics"].items():
        margin = max(st["std"], FLOORS[name])
        worst = None
        for i, m in measured:
            v = m[name]
            if name in ONE_SIDED_UPPER:
                dist = max(v - st["max"], 0.0)
            else:
                dist = max(st["min"] - v, v - st["max"], 0.0)
            if worst is None or dist > worst["dist"]:
                worst = {"frame": i, "value": v, "dist": dist}
        ratio = worst["dist"] / margin if margin else 0.0
        status = "pass" if ratio <= 1.0 else ("warn" if ratio <= 2.0 else "fail")
        if rank[status] > rank[worst_status]:
            worst_status = status
        results.append({"metric": name, "status": status, "worst_frame": worst["frame"],
                        "value": round(worst["value"], 3),
                        "anchor_range": [round(st["min"], 3), round(st["max"], 3)],
                        "margin": round(margin, 3), "deviation_in_margins": round(ratio, 2)})

    print(json.dumps({"status": worst_status, "fingerprint": args.fingerprint, "frames_measured": len(measured),
                      "metrics": results}, ensure_ascii=False, indent=2))
    return 1 if worst_status == "fail" else 0


def main():
    parser = argparse.ArgumentParser()
    sub = parser.add_subparsers(dest="cmd", required=True)
    b = sub.add_parser("build")
    b.add_argument("anchors", nargs="+")
    b.add_argument("--out", required=True)
    b.add_argument("--frame-size", nargs=2, type=int, metavar=("W", "H"))
    c = sub.add_parser("check")
    c.add_argument("image")
    c.add_argument("--fingerprint", required=True)
    c.add_argument("--frame-size", nargs=2, type=int, metavar=("W", "H"))
    args = parser.parse_args()
    return cmd_build(args) if args.cmd == "build" else cmd_check(args)


if __name__ == "__main__":
    sys.exit(main())
