#!/usr/bin/env python3
"""결정론적 에셋 검수. AI 가 아니라 코드가 판정한다.

사용법:
  python scripts/validate_asset.py IMAGE --profile character_sheet [--rules style/rules.json]
                                   [--frame-size W H]

결과는 JSON 으로 stdout 에 출력되고, 하나라도 fail 이면 종료 코드 1 이다.
"""
import argparse
import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image


def hex_to_rgb(value):
    value = value.lstrip("#")
    return tuple(int(value[i:i + 2], 16) for i in (0, 2, 4))


class Report:
    def __init__(self):
        self.checks = []

    def add(self, name, ok, detail="", level="fail"):
        self.checks.append({"check": name, "status": "pass" if ok else level, "detail": detail})

    @property
    def failed(self):
        return any(c["status"] == "fail" for c in self.checks)


def load_palette(rules, rules_path):
    palette_file = rules.get("palette_file")
    if not palette_file:
        return []
    path = Path(palette_file)
    if not path.is_absolute() and not path.exists():
        # rules.json 이 style/ 안에 있어도 프로젝트 루트 기준 경로를 쓸 수 있게 한다
        path = Path(rules_path).resolve().parent.parent / palette_file
    if not path.exists():
        return []
    data = json.loads(path.read_text(encoding="utf-8"))
    return [hex_to_rgb(c) for c in data.get("colors", [])]


def detect_upscale(arr):
    """이미지가 정수배 최근접 확대본이면 그 배율을 반환한다."""
    h, w = arr.shape[:2]
    for k in range(2, 9):
        if h % k or w % k:
            continue
        small = arr[::k, ::k]
        up = np.repeat(np.repeat(small, k, axis=0), k, axis=1)
        if np.array_equal(up, arr):
            return k
    return None


def check_palette(rgb, counts, palette, tolerance):
    """불투명 픽셀 중 팔레트와 (허용 오차 내) 일치하는 비율과 벗어난 색 상위 목록."""
    pal = np.array(palette, dtype=np.int32)
    dist = np.sqrt(((rgb[:, None, :].astype(np.int32) - pal[None, :, :]) ** 2).sum(axis=2)).min(axis=1)
    ok_mask = dist <= tolerance
    total = counts.sum()
    fraction = counts[ok_mask].sum() / total if total else 1.0
    bad = [(tuple(int(v) for v in rgb[i]), int(counts[i])) for i in np.argsort(-counts) if not ok_mask[i]][:5]
    return fraction, bad


def split_frames(arr, fw, fh):
    h, w = arr.shape[:2]
    cols, rows = w // fw, h // fh
    return [arr[r * fh:(r + 1) * fh, c * fw:(c + 1) * fw] for r in range(rows) for c in range(cols)]


def check_frames(report, arr, fw, fh, profile, expected_frames=None):
    h, w = arr.shape[:2]
    report.add("frame_grid", w % fw == 0 and h % fh == 0,
               f"이미지 {w}x{h}, 프레임 {fw}x{fh}")
    if w % fw or h % fh:
        return
    frames = split_frames(arr, fw, fh)
    if expected_frames is not None:
        report.add("frame_count_exact", len(frames) == expected_frames,
                   f"{len(frames)}프레임 (스펙 {expected_frames})")
    else:
        min_frames = profile.get("min_frames")
        if min_frames:
            report.add("frame_count", len(frames) >= min_frames, f"{len(frames)}프레임 (최소 {min_frames})")

    stats = []
    for i, f in enumerate(frames):
        ys, xs = np.nonzero(f[..., 3] > 0)
        if len(ys) == 0:
            stats.append(None)
            continue
        colors = {tuple(c) for c in np.unique(f[f[..., 3] > 0][:, :3], axis=0)}
        stats.append({"bottom": int(ys.max()), "cx": float(xs.mean()), "area": int(len(ys)), "colors": colors})

    empty = [i for i, s in enumerate(stats) if s is None]
    report.add("no_empty_frames", not empty, f"빈 프레임: {empty}" if empty else "")
    valid = [s for s in stats if s]
    if len(valid) < 2:
        return

    drift = max(s["bottom"] for s in valid) - min(s["bottom"] for s in valid)
    tol = profile.get("anchor_drift_px")
    if tol is not None:
        report.add("anchor_drift", drift <= tol, f"발바닥(하단) 위치 편차 {drift}px (허용 {tol})")

    cx_drift = max(s["cx"] for s in valid) - min(s["cx"] for s in valid)
    tol = profile.get("centroid_drift_px")
    if tol is not None:
        report.add("centroid_drift", cx_drift <= tol, f"가로 무게중심 편차 {cx_drift:.1f}px (허용 {tol})")

    ratio = profile.get("area_ratio")
    if ratio:
        median = float(np.median([s["area"] for s in valid]))
        bad = [i for i, s in enumerate(stats) if s and not (ratio[0] <= s["area"] / median <= ratio[1])]
        report.add("area_ratio", not bad, f"면적이 중앙값 대비 범위 {ratio} 밖인 프레임: {bad}" if bad else "")

    overlap = profile.get("palette_overlap")
    if overlap:
        base = valid[0]["colors"]
        worst = min(len(base & s["colors"]) / len(base | s["colors"]) for s in valid)
        report.add("palette_overlap", worst >= overlap, f"프레임 간 최저 팔레트 겹침 {worst:.2f} (최소 {overlap})")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("image")
    parser.add_argument("--profile", required=True)
    parser.add_argument("--rules", default="style/rules.json")
    parser.add_argument("--frame-size", nargs=2, type=int, metavar=("W", "H"))
    parser.add_argument("--expected-frames", type=int, help="시트의 총 프레임 수 (정확히 일치해야 통과)")
    parser.add_argument("--expected-size", nargs=2, type=int, metavar=("W", "H"),
                        help="단일 이미지의 스펙 canvas (정확히 일치해야 통과)")
    args = parser.parse_args()

    rules = json.loads(Path(args.rules).read_text(encoding="utf-8"))
    if args.profile not in rules["profiles"]:
        print(json.dumps({"error": f"profile '{args.profile}' 없음. 가능: {list(rules['profiles'])}"}, ensure_ascii=False))
        return 2
    profile = rules["profiles"][args.profile]
    palette = load_palette(rules, args.rules)

    report = Report()
    img = Image.open(args.image).convert("RGBA")
    arr = np.array(img)
    h, w = arr.shape[:2]

    frame_size = args.frame_size or profile.get("frame_size")
    sizes = profile.get("sizes")
    if sizes and not frame_size:
        report.add("size", [w, h] in sizes, f"{w}x{h} (허용 {sizes})")
    if args.expected_size and not frame_size:
        # 프로파일이 여러 크기를 허용해도, 이 에셋의 스펙이 요구한 크기와는 정확히 같아야 한다
        report.add("spec_canvas", [w, h] == list(args.expected_size), f"{w}x{h} (스펙 canvas {list(args.expected_size)})")

    alpha = arr[..., 3]
    if profile.get("alpha") == "binary":
        semi = int(((alpha > 0) & (alpha < 255)).sum())
        report.add("alpha_binary", semi == 0, f"반투명 픽셀 {semi}개")

    opaque = alpha > 0
    rgb_all = arr[opaque][:, :3]
    if len(rgb_all) == 0:
        report.add("not_empty", False, "불투명 픽셀이 없음")
    else:
        unique, counts = np.unique(rgb_all, axis=0, return_counts=True)
        max_colors = profile.get("max_colors")
        if max_colors:
            report.add("max_colors", len(unique) <= max_colors, f"{len(unique)}색 (최대 {max_colors})")
        if profile.get("palette_match") is not None:
            if not palette:
                report.add("palette", False, "palette.json 이 비어 있어 건너뜀. 팔레트를 채우세요", level="warn")
            else:
                fraction, bad = check_palette(unique, counts, palette, profile.get("palette_tolerance", 0))
                detail = f"팔레트 일치 {fraction:.1%} (필요 {profile['palette_match']:.0%})"
                if bad:
                    detail += f", 벗어난 색 상위: {bad}"
                report.add("palette", fraction >= profile["palette_match"], detail)

    if profile.get("forbid_upscaled"):
        k = detect_upscale(arr)
        report.add("native_pixel_grid", k is None,
                   f"정수배 확대본으로 보임 (x{k}). 네이티브 해상도로 만들거나 축소하세요" if k else "")

    if frame_size:
        check_frames(report, arr, frame_size[0], frame_size[1], profile, args.expected_frames)

    result = {"file": args.image, "profile": args.profile, "passed": not report.failed, "checks": report.checks}
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 1 if report.failed else 0


if __name__ == "__main__":
    sys.exit(main())
