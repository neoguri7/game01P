#!/usr/bin/env python3
"""승인 처리와 무결성 검증. 사람이 "승인"이라고 말한 뒤에만 실행한다.

  approve.py approve ID FILE [--manifest assets/manifest.json]
      FILE 을 assets/approved/ 로 옮기고 읽기 전용으로 잠그고, sha256 과 함께 manifest 에 기록한다.
  approve.py verify [--manifest assets/manifest.json]
      approved 상태인 모든 에셋의 파일이 존재하고 해시가 기록과 같은지 확인한다.
"""
import argparse
import datetime
import hashlib
import json
import os
import shutil
import stat
import sys
from pathlib import Path


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def load(path):
    return json.loads(Path(path).read_text(encoding="utf-8"))


def save(path, data):
    Path(path).write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def cmd_approve(args):
    manifest = load(args.manifest)
    entry = next((a for a in manifest["assets"] if a.get("id") == args.id), None)
    if entry is None:
        print(f"오류: manifest 에 id '{args.id}' 가 없습니다. 먼저 항목을 등록하세요.")
        return 2
    src = Path(args.file)
    if not src.exists():
        print(f"오류: 파일이 없습니다: {src}")
        return 2
    qa = src.with_suffix(".qa.json")
    if not qa.exists() or not load(qa).get("passed"):
        print("오류: 이 파일의 통과한 QA 결과(.qa.json)가 없습니다. qa_gate.py 를 먼저 통과시키세요.")
        return 2
    if load(qa).get("sha256") != sha256(src):
        print("오류: QA 이후 파일이 바뀌었습니다 (해시 불일치). qa_gate.py 를 다시 실행하세요.")
        return 2
    dst_dir = Path(args.approved_dir)
    dst_dir.mkdir(parents=True, exist_ok=True)
    dst = dst_dir / f"{args.id}{src.suffix}"
    if dst.exists():
        print(f"오류: 이미 승인된 파일이 있습니다: {dst} (덮어쓰지 않습니다)")
        return 2
    shutil.copy2(src, dst)
    os.chmod(dst, stat.S_IRUSR | stat.S_IRGRP | stat.S_IROTH)  # 0444
    entry.update({"status": "approved", "file": str(dst), "sha256": sha256(dst),
                  "approved_at": datetime.datetime.now(datetime.timezone.utc).isoformat(timespec="seconds"),
                  "source_attempt_file": str(src)})
    save(args.manifest, manifest)
    print(f"승인 완료: {dst} (sha256 {entry['sha256'][:12]}…)")
    return 0


def cmd_verify(args):
    manifest = load(args.manifest)
    problems, count = [], 0
    for a in manifest["assets"]:
        if a.get("status") != "approved":
            continue
        count += 1
        f = Path(a.get("file", ""))
        if not f.exists():
            problems.append(f"{a['id']}: 파일 없음 ({f})")
        elif sha256(f) != a.get("sha256"):
            problems.append(f"{a['id']}: 해시 불일치 — 승인 후 파일이 바뀜 ({f})")
    if problems:
        print("검증 실패:")
        for p in problems:
            print("  -", p)
        return 1
    print(f"검증 통과: 승인 에셋 {count}개 모두 무결")
    return 0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", default="assets/manifest.json")
    parser.add_argument("--approved-dir", default="assets/approved")
    sub = parser.add_subparsers(dest="cmd", required=True)
    a = sub.add_parser("approve")
    a.add_argument("id")
    a.add_argument("file")
    sub.add_parser("verify")
    args = parser.parse_args()
    return cmd_approve(args) if args.cmd == "approve" else cmd_verify(args)


if __name__ == "__main__":
    sys.exit(main())
