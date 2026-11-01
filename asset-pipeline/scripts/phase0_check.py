#!/usr/bin/env python3
"""Phase 0 (스타일 잠금) 가 끝났는지 코드로 확인한다. /make-asset 의 첫 단계가 이것을 호출한다.

  phase0_check.py [--kind PROFILE ...] [--rules style/rules.json] [--bible style/STYLE_BIBLE.md]

--kind 를 주면 그 프로파일만, 주지 않으면 rules.json 의 모든 프로파일을 점검한다.

공통 점검
  - 스타일 바이블: 초안 표시(<!-- DRAFT)가 지워졌고, "프롬프트 고정 문구" 블록이 채워졌다
  - 팔레트: palette.json 에 색이 1개 이상 있다
종류(프로파일)별 점검
  - 앵커: style/anchors/<지문 파일 이름>/ 에 이미지 3장 이상 (스프라이트와 시트는 같은 캐릭터 지문을 공유한다)
  - 지문: 프로파일의 fingerprint 파일이 있고, 만든 뒤 앵커가 바뀌지 않았다 (바뀌었으면 경고)

바이블의 초안 표시와 팔레트는 사람이 정한다. 이 스크립트는 바꾸지 않고 읽기만 한다.
종료 코드: 막는 항목(FAIL)이 하나라도 있으면 1.
"""
import argparse
import json
import re
import sys
from pathlib import Path

MIN_ANCHORS = 3
RECOMMENDED_ANCHORS = 5
IMAGE_SUFFIXES = {".png"}


def load_palette_colors(rules, rules_path):
    palette_file = rules.get("palette_file")
    if not palette_file:
        return None
    path = Path(palette_file)
    if not path.is_absolute() and not path.exists():
        path = Path(rules_path).resolve().parent.parent / palette_file
    if not path.exists():
        return None
    return json.loads(path.read_text(encoding="utf-8")).get("colors", [])


def fixed_phrase(bible_text):
    """'프롬프트 고정 문구' 절의 첫 코드 블록 내용. 없으면 None."""
    m = re.search(r"##\s*프롬프트 고정 문구.*?```[^\n]*\n(.*?)\n```", bible_text, re.DOTALL)
    return m.group(1).strip() if m else None


def check_bible(path):
    results = []
    if not path.exists():
        return [("FAIL", "스타일 바이블", f"파일이 없음: {path}")]
    text = path.read_text(encoding="utf-8")
    if "<!-- DRAFT" in text:
        results.append(("FAIL", "스타일 바이블", "초안 표시(<!-- DRAFT ...)가 남아 있음. 사람이 내용을 검토하고 그 줄을 지워야 잠금이 끝난다"))
    else:
        results.append(("OK", "스타일 바이블", "초안 표시 없음"))
    phrase = fixed_phrase(text)
    if not phrase or phrase.startswith("(여기에"):
        results.append(("FAIL", "프롬프트 고정 문구", "비어 있거나 안내 문구 그대로임"))
    else:
        results.append(("OK", "프롬프트 고정 문구", phrase if len(phrase) <= 100 else phrase[:97] + "..."))
    return results


def check_palette(rules, rules_path):
    colors = load_palette_colors(rules, rules_path)
    if colors is None:
        return [("FAIL", "팔레트", "palette_file 을 찾을 수 없음")]
    if not colors:
        return [("FAIL", "팔레트", "palette.json 에 색이 없음. 팔레트 검사가 꺼진 상태로 통과되므로 사람이 채워야 한다")]
    return [("OK", "팔레트", f"{len(colors)}색")]


def check_kind(kind, profile, style_dir):
    fp_rel = profile.get("fingerprint") or f"style/fingerprints/{kind}.json"
    fp_path = Path(fp_rel)
    anchors_dir = style_dir / "anchors" / fp_path.stem
    results = []

    anchors = sorted(p for p in anchors_dir.glob("*") if p.suffix.lower() in IMAGE_SUFFIXES) if anchors_dir.is_dir() else []
    if len(anchors) < MIN_ANCHORS:
        results.append(("FAIL", "앵커", f"{anchors_dir} 에 이미지 {len(anchors)}장 (최소 {MIN_ANCHORS}장, 권장 {RECOMMENDED_ANCHORS}~10장)"))
    elif len(anchors) < RECOMMENDED_ANCHORS:
        results.append(("OK", "앵커", f"{len(anchors)}장 ({anchors_dir}) — 권장 {RECOMMENDED_ANCHORS}장 미만이라 지문 범위가 좁을 수 있음"))
    else:
        results.append(("OK", "앵커", f"{len(anchors)}장 ({anchors_dir})"))

    if not fp_path.exists():
        results.append(("FAIL", "지문", f"{fp_rel} 없음. style_fingerprint.py build 로 만들어야 함"))
    else:
        fp = json.loads(fp_path.read_text(encoding="utf-8"))
        built_from = {Path(a).name for a in fp.get("anchors", [])}
        now = {p.name for p in anchors}
        if built_from != now:
            results.append(("WARN", "지문", f"{fp_rel} 를 만든 뒤 앵커가 바뀐 것 같음 (지문 {len(built_from)}장 / 현재 {len(now)}장). 다시 만들 것"))
        else:
            results.append(("OK", "지문", f"{fp_rel} (표본 {fp.get('samples')}개)"))
    return results


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--kind", nargs="+", help="점검할 rules.json 프로파일 이름 (기본: 전부)")
    parser.add_argument("--rules", default="style/rules.json")
    parser.add_argument("--bible", help="기본: <rules 가 있는 폴더>/STYLE_BIBLE.md")
    args = parser.parse_args()

    rules_path = Path(args.rules)
    rules = json.loads(rules_path.read_text(encoding="utf-8"))
    style_dir = rules_path.resolve().parent
    profiles = rules.get("profiles", {})
    kinds = args.kind or list(profiles)
    unknown = [k for k in kinds if k not in profiles]
    if unknown:
        print(f"오류: rules.json 에 없는 프로파일: {unknown} (가능: {list(profiles)})")
        return 2

    sections = [("공통", check_bible(Path(args.bible) if args.bible else style_dir / "STYLE_BIBLE.md")
                 + check_palette(rules, rules_path))]
    sections += [(k, check_kind(k, profiles[k], style_dir)) for k in kinds]

    failed = False
    for title, items in sections:
        print(f"== {title}")
        for status, name, detail in items:
            print(f"  [{status:<4}] {name}: {detail}")
            failed |= status == "FAIL"
    print("\nPhase 0 " + ("미완료 — 위 FAIL 항목을 사람이 해결해야 에셋을 만들 수 있다" if failed else "완료"))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
