#!/usr/bin/env python3
"""한 번에 돌리는 QA 게이트. 에이전트는 개별 검수 스크립트 대신 이것만 호출한다.

  qa_gate.py IMAGE --spec specs/<id>.json [--fingerprint style/fingerprints/<kind>.json]

하는 일 (순서대로):
  1. parent_approved : 스펙에 parent 가 있으면 manifest 에서 승인(approved) 상태인지 확인
  2. validate_asset  : 알파, 팔레트, 격자, 색 수, 프레임 수/편차 같은 규칙 검사
  3. style_fingerprint: 앵커 대비 스타일 수치 검사 (지문 파일이 있을 때)

결과는 IMAGE 옆에 <이름>.qa.json 으로 저장하고, 요약을 출력한다.
종료 코드: 실패가 있으면 1, 모두 통과(경고 포함)면 0.
"""
import argparse
import hashlib
import json
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent


def run_json(cmd):
    proc = subprocess.run(cmd, capture_output=True, text=True)
    try:
        return json.loads(proc.stdout), proc.returncode
    except json.JSONDecodeError:
        return {"error": (proc.stdout + proc.stderr).strip()[:2000]}, proc.returncode or 2


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("image")
    parser.add_argument("--spec", required=True)
    parser.add_argument("--rules", default="style/rules.json")
    parser.add_argument("--manifest", default="assets/manifest.json")
    parser.add_argument("--fingerprint")
    args = parser.parse_args()

    spec = json.loads(Path(args.spec).read_text(encoding="utf-8"))
    kind = spec["kind"]
    rules = json.loads(Path(args.rules).read_text(encoding="utf-8"))
    if kind not in rules.get("profiles", {}):
        print(f"QA FAIL: 스펙의 kind '{kind}' 가 {args.rules} 의 profiles 에 없습니다 (가능: {list(rules.get('profiles', {}))})")
        return 1
    is_sheet = spec.get("layout") == "sheet"
    summary, failed = [], False

    # 1. 부모(마스터) 승인 게이트
    parent = spec.get("parent")
    if parent:
        manifest = json.loads(Path(args.manifest).read_text(encoding="utf-8")) if Path(args.manifest).exists() else {"assets": []}
        entry = next((a for a in manifest["assets"] if a.get("id") == parent), None)
        ok = bool(entry and entry.get("status") == "approved")
        summary.append({"check": "parent_approved", "status": "pass" if ok else "fail",
                        "detail": f"parent={parent}, status={entry.get('status') if entry else '없음'}"})
        failed |= not ok

    # 2. 규칙 검사
    cmd = [sys.executable, str(HERE / "validate_asset.py"), args.image, "--profile", kind, "--rules", args.rules]
    if is_sheet:
        cmd += ["--frame-size", str(spec["canvas"][0]), str(spec["canvas"][1])]
        if spec.get("frames"):
            cmd += ["--expected-frames", str(spec["frames"])]
    elif spec.get("canvas"):
        cmd += ["--expected-size", str(spec["canvas"][0]), str(spec["canvas"][1])]
    validate, rc = run_json(cmd)
    if "checks" in validate:
        for c in validate["checks"]:
            if c["status"] != "pass":
                summary.append({"check": f"validate.{c['check']}", "status": c["status"], "detail": c["detail"]})
        failed |= not validate["passed"]
    else:
        summary.append({"check": "validate", "status": "fail", "detail": str(validate)})
        failed = True

    # 3. 스타일 지문
    # 지문 경로 우선순위: 명령행 > rules.json 프로파일의 fingerprint > style/fingerprints/<kind>.json
    fp_path = args.fingerprint or rules["profiles"][kind].get("fingerprint") or f"style/fingerprints/{kind}.json"
    cmd = [sys.executable, str(HERE / "style_fingerprint.py"), "check", args.image, "--fingerprint", fp_path]
    if is_sheet:
        cmd += ["--frame-size", str(spec["canvas"][0]), str(spec["canvas"][1])]
    fingerprint, rc = run_json(cmd)
    if fingerprint.get("status") == "skipped":
        summary.append({"check": "fingerprint", "status": "warn", "detail": fingerprint["reason"] + " (스타일 수치 검사를 건너뜀)"})
    elif "metrics" in fingerprint:
        for m in fingerprint["metrics"]:
            if m["status"] != "pass":
                summary.append({"check": f"fingerprint.{m['metric']}", "status": m["status"],
                                "detail": f"값 {m['value']} (앵커 범위 {m['anchor_range']}, 프레임 {m['worst_frame']}, 허용폭의 {m['deviation_in_margins']}배)"})
        failed |= fingerprint["status"] == "fail"
    else:
        summary.append({"check": "fingerprint", "status": "fail", "detail": str(fingerprint)})
        failed = True

    image_hash = hashlib.sha256(Path(args.image).read_bytes()).hexdigest()
    result = {"file": args.image, "sha256": image_hash, "spec": args.spec, "passed": not failed,
              "issues": summary, "validate": validate, "fingerprint": fingerprint}
    out_path = Path(args.image).with_suffix(".qa.json")
    out_path.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")

    print(f"QA {'PASS' if not failed else 'FAIL'}: {args.image}")
    for s in summary:
        print(f"  [{s['status'].upper()}] {s['check']}: {s['detail']}")
    print(f"  상세: {out_path}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
