#!/usr/bin/env python3
"""reference/sources.json 의 외부 게임 스크린샷을 reference/local/ 로 내려받는다 (git 이 무시하는 폴더).

  fetch_references.py [--out reference/local] [--sources reference/sources.json] [--only unit background ...]

- 이미지는 저작권물이다. 저장소에 커밋하지 않는다 (reference/local/ 은 .gitignore 대상).
- 이미 있는 파일은 건너뛰고, 받은 파일은 이미지로 열리는지 확인한다.
- 이 이미지들은 눈으로 보고 규칙을 뽑거나, 생성 도구의 스타일 입력으로만 쓴다. 앵커나 img2img 입력으로 쓰지 않는다 (CLAUDE.md 규칙 13).
종료 코드: 하나라도 실패하면 1.
"""
import argparse
import json
import sys
import urllib.request
from pathlib import Path

from PIL import Image


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--sources", default="reference/sources.json")
    parser.add_argument("--out", default="reference/local")
    parser.add_argument("--only", nargs="+", help="이 태그 중 하나라도 가진 항목만 (unit monster background object effect ui)")
    args = parser.parse_args()

    items = json.loads(Path(args.sources).read_text(encoding="utf-8"))["images"]
    items = [i for i in items if "skip" not in i["tags"]]
    if args.only:
        items = [i for i in items if set(i["tags"]) & set(args.only)]
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)

    failed = 0
    for item in items:
        dst = out / f"{item['id']}.jpg"
        if dst.exists():
            print(f"건너뜀 {dst.name}")
            continue
        try:
            req = urllib.request.Request(item["url"], headers={"User-Agent": "asset-pipeline-reference-fetch"})
            with urllib.request.urlopen(req, timeout=60) as r:
                data = r.read()
            dst.write_bytes(data)
            with Image.open(dst) as im:
                im.verify()
            print(f"받음   {dst.name} ({len(data) // 1024} KB)")
        except Exception as e:  # 네트워크 정책/호스트 거부도 여기로 온다. 우회하지 않고 보고한다
            failed += 1
            dst.unlink(missing_ok=True)
            print(f"실패   {item['id']}: {e}")
    print(f"완료: {len(items) - failed}/{len(items)}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
