#!/usr/bin/env python3
"""파이프라인 스모크 테스트. 합성 스프라이트로 게이트가 실제로 통과/실패를 가르는지 확인한다.

  python tests/test_pipeline.py            (asset-pipeline/ 에서 실행. pip install pillow numpy)

실제 style/, assets/ 는 건드리지 않는다. 테스트마다 임시 작업공간을 만들고 그 안에서 스크립트를 실행한다.
rules.json 은 프로젝트 것을 복사해 쓰되 팔레트만 테스트용으로 바꾼다 (프로젝트 팔레트는 사람이 정한다).
"""
import json
import os
import shutil
import stat
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
SCRIPTS = ROOT / "scripts"

OUTLINE = (28, 20, 24)
GREEN = [(92, 120, 70), (66, 88, 52), (44, 60, 38)]
RED = [(150, 60, 50), (110, 42, 38), (74, 30, 30)]
SKIN = [(200, 160, 120), (160, 120, 90)]
# 앵커가 쓰지 않는 밝고 채도 높은 색도 팔레트에 넣어 둔다. 팔레트는 통과하지만 스타일은 어긋난 이미지를 만들기 위해서다.
NEON = [(255, 60, 255), (60, 255, 255), (255, 255, 60)]
PALETTE = [OUTLINE, *GREEN, *RED, *SKIN, *NEON]


def hexcolor(c):
    return "#%02x%02x%02x" % c


def outline_pass(arr, color=OUTLINE):
    """불투명 영역의 가장자리 픽셀을 외곽선 색으로 덮는다 (실루엣은 그대로)."""
    opaque = arr[..., 3] > 0
    padded = np.pad(opaque, 1)
    inner = padded[:-2, 1:-1] & padded[2:, 1:-1] & padded[1:-1, :-2] & padded[1:-1, 2:]
    edge = opaque & ~inner
    arr[edge] = (*color, 255)
    return arr


def sprite(seed, size=64, dy=0, neon=False, outline=True, noise_colors=0):
    """좌상단 광원, 3단계 명암, 1px 외곽선의 간단한 인물. seed 로 체형이 조금씩 다르다."""
    rng = np.random.default_rng(seed)
    arr = np.zeros((size, size, 4), np.uint8)
    bw, bh = 20 + int(rng.integers(-1, 2)), 30 + int(rng.integers(-1, 2))
    x0, y1 = (size - bw) // 2, size - 6 + dy
    y0 = y1 - bh
    for y in range(y0, y1):
        for x in range(x0, x0 + bw):
            t = ((x - x0) / bw + (y - y0) / bh) / 2.0
            tone = 0 if t < 0.34 else (1 if t < 0.67 else 2)
            colors = RED if y < y0 + bh // 2 else GREEN
            arr[y, x] = (*(NEON[tone] if neon else colors[tone]), 255)
    hx0, hy0 = x0 + 4, y0 - 10
    for y in range(hy0, y0):
        for x in range(hx0, hx0 + bw - 8):
            arr[y, x] = (*SKIN[0 if (x - hx0) < (bw - 8) // 2 else 1], 255)
    if outline:
        outline_pass(arr)
    if noise_colors:
        ys, xs = np.nonzero(arr[..., 3] > 0)
        for i in range(noise_colors):
            j = int(rng.integers(0, len(ys)))
            arr[ys[j], xs[j], :3] = rng.integers(0, 256, 3)
    return arr


def sheet(frames=4, **kw):
    """프레임을 가로 한 줄로 이어 붙인 시트. frames 번째마다 팔 한 점만 움직인다."""
    out = []
    for i in range(frames):
        f = sprite(7, **kw)
        f[30, 24 + i % 3] = (*SKIN[0], 255)
        out.append(f)
    return np.concatenate(out, axis=1)


def save(arr, path):
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    Image.fromarray(arr).save(path)
    return str(path)


class Workspace:
    """임시 작업공간: 프로젝트 rules.json + 테스트 팔레트 + 빈 manifest."""

    def __init__(self):
        self.dir = Path(tempfile.mkdtemp(prefix="pipeline-test-"))
        (self.dir / "style" / "fingerprints").mkdir(parents=True)
        (self.dir / "specs").mkdir()
        (self.dir / "assets").mkdir()
        shutil.copy(ROOT / "style" / "rules.json", self.dir / "style" / "rules.json")
        self.write_json("style/palette.json", {"name": "test", "colors": [hexcolor(c) for c in PALETTE]})
        self.write_json("assets/manifest.json", {"assets": []})

    def p(self, rel):
        return self.dir / rel

    def write_json(self, rel, data):
        self.p(rel).parent.mkdir(parents=True, exist_ok=True)
        self.p(rel).write_text(json.dumps(data, ensure_ascii=False, indent=2), encoding="utf-8")

    def read_json(self, rel):
        return json.loads(self.p(rel).read_text(encoding="utf-8"))

    def run(self, script, *args):
        return subprocess.run([sys.executable, str(SCRIPTS / script), *map(str, args)],
                              cwd=self.dir, capture_output=True, text=True)

    def build_character_fingerprint(self, n=4):
        anchors = [save(sprite(s), self.p(f"style/anchors/character/a{s}.png")) for s in range(1, n + 1)]
        return self.run("style_fingerprint.py", "build", *anchors, "--out", "style/fingerprints/character.json")

    def spec(self, **over):
        s = {"id": "hero", "kind": "character_sprite", "layout": "single", "canvas": [64, 64],
             "prompt_asset": "x", "candidates": 1, "max_attempts": 3, "acceptance": ["x"]}
        s.update(over)
        self.write_json(f"specs/{s['id']}.json", s)
        return f"specs/{s['id']}.json"

    def qa(self, img, spec, **flags):
        r = self.run("qa_gate.py", img, "--spec", spec)
        qa = self.p(img).with_suffix(".qa.json")
        return r, (json.loads(qa.read_text(encoding="utf-8")) if qa.exists() else None)

    def cleanup(self):
        shutil.rmtree(self.dir, ignore_errors=True)


def failed_checks(qa):
    return {i["check"] for i in qa["issues"] if i["status"] == "fail"}


class PipelineTest(unittest.TestCase):
    def setUp(self):
        self.ws = Workspace()
        self.addCleanup(self.ws.cleanup)

    # ---- 스타일 지문 ----
    def test_fingerprint_needs_three_samples(self):
        r = self.ws.build_character_fingerprint(n=2)
        self.assertEqual(r.returncode, 2, r.stdout)
        r = self.ws.build_character_fingerprint(n=4)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertTrue(self.ws.p("style/fingerprints/character.json").exists())

    def test_fingerprint_build_expands_wildcards_itself(self):
        """PowerShell/cmd 는 glob 을 펼치지 않으므로 스크립트가 직접 펼쳐야 한다 (셸 없이 호출해 재현)."""
        for s in range(1, 5):
            save(sprite(s), self.ws.p(f"style/anchors/character/a{s}.png"))
        r = self.ws.run("style_fingerprint.py", "build", "style/anchors/character/*.png",
                        "--out", "style/fingerprints/character.json")
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertEqual(self.ws.read_json("style/fingerprints/character.json")["samples"], 4)
        r = self.ws.run("style_fingerprint.py", "build", "style/anchors/none/*.png", "--out", "x.json")
        self.assertEqual(r.returncode, 2, "일치하는 파일이 없으면 오류여야 한다")

    # ---- QA 게이트: 통과 / 실패 구분 ----
    def test_qa_passes_clean_candidate_and_fails_each_defect(self):
        self.ws.build_character_fingerprint()
        spec = self.ws.spec()

        good = save(sprite(10), self.ws.p("assets/raw/hero/good.png"))
        r, qa = self.ws.qa(good, spec)
        self.assertEqual(r.returncode, 0, r.stdout)
        self.assertTrue(qa["passed"])

        semi = sprite(10)
        semi[40, 32, 3] = 120
        r, qa = self.ws.qa(save(semi, self.ws.p("assets/raw/hero/semi.png")), spec)
        self.assertEqual(r.returncode, 1)
        self.assertIn("validate.alpha_binary", failed_checks(qa))

        off = sprite(10)
        off[40, 32, :3] = (13, 200, 77)
        r, qa = self.ws.qa(save(off, self.ws.p("assets/raw/hero/off.png")), spec)
        self.assertIn("validate.palette", failed_checks(qa))

        up = np.repeat(np.repeat(sprite(10, size=32), 2, axis=0), 2, axis=1)
        r, qa = self.ws.qa(save(up, self.ws.p("assets/raw/hero/up.png")), spec)
        self.assertIn("validate.native_pixel_grid", failed_checks(qa))

        small = sprite(10, size=40)  # 어느 프로파일도 허용하지 않는 크기
        r, qa = self.ws.qa(save(small, self.ws.p("assets/raw/hero/small.png")), spec)
        self.assertIn("validate.size", failed_checks(qa))

    def test_qa_fingerprint_catches_style_drift_that_passes_the_rules(self):
        self.ws.build_character_fingerprint()
        spec = self.ws.spec()
        drift = sprite(10, neon=True, outline=False)
        r, qa = self.ws.qa(save(drift, self.ws.p("assets/raw/hero/drift.png")), spec)
        self.assertFalse(qa["passed"])
        self.assertTrue(any(c.startswith("fingerprint.") for c in failed_checks(qa)), qa["issues"])
        self.assertFalse(any(c.startswith("validate.") for c in failed_checks(qa)),
                         "규칙 검사는 통과해야 이 테스트가 지문 검사만 본다")

    def test_qa_enforces_spec_canvas_for_single_images(self):
        """스펙이 32x32 를 요구하는데 프로파일이 허용하는 다른 크기(64x64)가 나오면 실패해야 한다."""
        self.ws.build_character_fingerprint()
        spec = self.ws.spec(canvas=[32, 32])
        img = save(sprite(10), self.ws.p("assets/raw/hero/a1.png"))
        r, qa = self.ws.qa(img, spec)
        self.assertFalse(qa["passed"], "스펙 canvas 와 다른 크기가 통과함")
        self.assertIn("validate.spec_canvas", failed_checks(qa))

    # ---- 시트 ----
    def test_sheet_checks(self):
        self.ws.build_character_fingerprint()
        spec = self.ws.spec(id="walk", kind="character_sheet", layout="sheet", frames=4, directions=1)

        r, qa = self.ws.qa(save(sheet(4), self.ws.p("assets/raw/walk/ok.png")), spec)
        self.assertEqual(r.returncode, 0, r.stdout)

        r, qa = self.ws.qa(save(sheet(3), self.ws.p("assets/raw/walk/three.png")), spec)
        self.assertIn("validate.frame_count_exact", failed_checks(qa))

        f = [sprite(7), sprite(7), sprite(7, dy=3), sprite(7)]
        r, qa = self.ws.qa(save(np.concatenate(f, axis=1), self.ws.p("assets/raw/walk/drift.png")), spec)
        self.assertIn("validate.anchor_drift", failed_checks(qa))

        f = [sprite(7), np.zeros((64, 64, 4), np.uint8), sprite(7), sprite(7)]
        r, qa = self.ws.qa(save(np.concatenate(f, axis=1), self.ws.p("assets/raw/walk/empty.png")), spec)
        self.assertIn("validate.no_empty_frames", failed_checks(qa))

    # ---- 파생 잠금 ----
    def test_parent_must_be_approved(self):
        self.ws.build_character_fingerprint()
        spec = self.ws.spec(id="walk", kind="character_sheet", layout="sheet", frames=4, parent="hero")
        img = save(sheet(4), self.ws.p("assets/raw/walk/a1.png"))
        r, qa = self.ws.qa(img, spec)
        self.assertIn("parent_approved", failed_checks(qa))

        self.ws.write_json("assets/manifest.json", {"assets": [{"id": "hero", "status": "approved"}]})
        r, qa = self.ws.qa(img, spec)
        self.assertNotIn("parent_approved", failed_checks(qa))

    # ---- 결정론적 보정 ----
    def test_snap_palette_fixes_small_error_and_refuses_foreign_colors(self):
        noisy = sprite(10).astype(np.int16)
        noisy[..., :3] += np.where(noisy[..., 3:4] > 0, 3, 0)
        noisy[5, 5, 3] = 90
        src = save(np.clip(noisy, 0, 255).astype(np.uint8), self.ws.p("assets/raw/hero/noisy.png"))
        r = self.ws.run("snap_palette.py", src, "assets/raw/hero/snapped.png")
        self.assertEqual(r.returncode, 0, r.stdout)
        a = np.array(Image.open(self.ws.p("assets/raw/hero/snapped.png")).convert("RGBA"))
        self.assertEqual(int(((a[..., 3] > 0) & (a[..., 3] < 255)).sum()), 0)

        rng = np.random.default_rng(3)
        foreign = sprite(10)
        m = foreign[..., 3] > 0
        foreign[m, :3] = rng.integers(0, 256, (int(m.sum()), 3))
        src = save(foreign, self.ws.p("assets/raw/hero/foreign.png"))
        r = self.ws.run("snap_palette.py", src, "assets/raw/hero/foreign_out.png")
        self.assertEqual(r.returncode, 3, r.stdout)
        self.assertFalse(self.ws.p("assets/raw/hero/foreign_out.png").exists())

    # ---- 승인과 무결성 ----
    def test_approve_requires_passing_qa_and_locks_the_file(self):
        self.ws.build_character_fingerprint()
        spec = self.ws.spec()
        self.ws.write_json("assets/manifest.json", {"assets": [{"id": "hero", "status": "needs_review"}]})
        cand = save(sprite(10), self.ws.p("assets/candidates/hero/a1.png"))

        r = self.ws.run("approve.py", "approve", "hero", cand)
        self.assertEqual(r.returncode, 2, "QA 없이 승인됨")

        r, qa = self.ws.qa(cand, spec)
        self.assertTrue(qa["passed"])
        r = self.ws.run("approve.py", "approve", "hero", cand)
        self.assertEqual(r.returncode, 0, r.stdout)
        approved = self.ws.p("assets/approved/hero.png")
        self.assertTrue(approved.exists())
        if os.name == "posix":
            self.assertEqual(stat.S_IMODE(approved.stat().st_mode), 0o444)
        entry = self.ws.read_json("assets/manifest.json")["assets"][0]
        self.assertEqual(entry["status"], "approved")
        self.assertEqual(len(entry["sha256"]), 64)
        self.assertEqual(self.ws.run("approve.py", "verify").returncode, 0)

        # 다시 승인(덮어쓰기)은 거부
        self.assertEqual(self.ws.run("approve.py", "approve", "hero", cand).returncode, 2)

        # 승인본이 몰래 바뀌면 verify 가 잡는다
        os.chmod(approved, 0o644)
        with open(approved, "ab") as f:
            f.write(b"x")
        r = self.ws.run("approve.py", "verify")
        self.assertEqual(r.returncode, 1)
        self.assertIn("해시 불일치", r.stdout)

    def test_approve_rejects_file_changed_after_qa(self):
        self.ws.build_character_fingerprint()
        spec = self.ws.spec()
        self.ws.write_json("assets/manifest.json", {"assets": [{"id": "hero", "status": "needs_review"}]})
        cand = save(sprite(10), self.ws.p("assets/candidates/hero/a1.png"))
        self.ws.qa(cand, spec)
        save(sprite(11), cand)  # QA 이후 같은 이름으로 교체
        r = self.ws.run("approve.py", "approve", "hero", cand)
        self.assertEqual(r.returncode, 2)
        self.assertIn("해시 불일치", r.stdout)

    # ---- Phase 0 점검 ----
    BIBLE_OK = ("# 스타일 바이블\n\n## 프롬프트 고정 문구\n\n```\n"
                "dark fantasy forest, hard pixel edges\n```\n")

    def test_phase0_blocks_until_every_lock_is_in_place(self):
        ws = self.ws
        r = ws.run("phase0_check.py", "--kind", "character_sprite")
        self.assertEqual(r.returncode, 1, "바이블/앵커/지문이 없는데 통과함\n" + r.stdout)

        ws.p("style/STYLE_BIBLE.md").write_text("<!-- DRAFT: 사람 검토 전 -->\n" + self.BIBLE_OK, encoding="utf-8")
        self.assertEqual(ws.build_character_fingerprint().returncode, 0)
        r = ws.run("phase0_check.py", "--kind", "character_sprite")
        self.assertEqual(r.returncode, 1)
        self.assertIn("초안 표시", r.stdout)

        ws.p("style/STYLE_BIBLE.md").write_text(self.BIBLE_OK, encoding="utf-8")
        r = ws.run("phase0_check.py", "--kind", "character_sprite")
        self.assertEqual(r.returncode, 0, r.stdout)
        # 시트와 스프라이트는 같은 캐릭터 지문을 공유한다
        self.assertEqual(ws.run("phase0_check.py", "--kind", "character_sheet").returncode, 0)
        # 다른 종류(tile)는 앵커/지문이 없으므로 여전히 막힌다
        self.assertEqual(ws.run("phase0_check.py", "--kind", "tile").returncode, 1)

    def test_phase0_requires_palette_and_filled_phrase(self):
        ws = self.ws
        ws.build_character_fingerprint()
        ws.p("style/STYLE_BIBLE.md").write_text(self.BIBLE_OK, encoding="utf-8")
        ws.write_json("style/palette.json", {"name": "empty", "colors": []})
        r = ws.run("phase0_check.py", "--kind", "character_sprite")
        self.assertEqual(r.returncode, 1)
        self.assertIn("팔레트", r.stdout)

        ws.write_json("style/palette.json", {"name": "t", "colors": [hexcolor(c) for c in PALETTE]})
        ws.p("style/STYLE_BIBLE.md").write_text(
            "## 프롬프트 고정 문구\n\n```\n(여기에 고정 문구를 적는다. 예: \"...\")\n```\n", encoding="utf-8")
        r = ws.run("phase0_check.py", "--kind", "character_sprite")
        self.assertEqual(r.returncode, 1)
        self.assertIn("고정 문구", r.stdout)

    def test_phase0_warns_when_anchors_changed_after_fingerprint(self):
        ws = self.ws
        ws.build_character_fingerprint()
        ws.p("style/STYLE_BIBLE.md").write_text(self.BIBLE_OK, encoding="utf-8")
        save(sprite(99), ws.p("style/anchors/character/late.png"))
        r = ws.run("phase0_check.py", "--kind", "character_sprite")
        self.assertEqual(r.returncode, 0, "경고는 막지 않는다")
        self.assertIn("WARN", r.stdout)

    def test_phase0_rejects_unknown_kind(self):
        self.assertEqual(self.ws.run("phase0_check.py", "--kind", "dragon").returncode, 2)

    # ---- 비교 이미지 ----
    def test_contact_sheet(self):
        imgs = [save(sprite(s), self.ws.p(f"assets/raw/hero/c{s}.png")) for s in (1, 2, 3)]
        r = self.ws.run("contact_sheet.py", "assets/raw/hero/compare.png", *imgs)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertTrue(self.ws.p("assets/raw/hero/compare.png").exists())


if __name__ == "__main__":
    unittest.main(verbosity=2)
