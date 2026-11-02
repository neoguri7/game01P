# 탐색 라운드 기록 (앵커 부트스트랩, 옵션 B)

승인 대상이 아니다. 사람이 앵커를 고르기 위한 후보 모음이다. manifest 에는 올리지 않는다.
PixelLab: 2026-11-02, 로컬 환경 변수 토큰.

## 결정 (사용자, 2026-11-02)
- 앵커는 탐색 라운드로 만든다 (옵션 B).
- R1 카메라 규칙 승인: 새 에셋은 아이소 투영(64x32 다이아몬드)에 맞춘 3/4 시점.
- 아이소 바닥은 두께 없는 평면 다이아몬드로 바꾼다.
- 스타일 기준은 Pillars of Eternity / Divinity 공식 스크린샷 30장 (`reference/sources.json`). 스타일 입력으로만 사용.

## 방법 1: 텍스트만 (create_image_pixen, 64x64, 후보 2장/종, seed 1·2) - 16 generations
- 고정 문구에 `dark fantasy forest` 가 있어서 **나무 배경이 같이 그려지는 후보가 많다** (no_background 가 무시됨). 이 방식은 폐기.
- 결과: `character/*.png`, 한눈에: `sheet_textonly.png`

## 방법 2: 스크린샷 스타일 입력 (create_image_pro, 64x64, 후보 16장/종, seed 1) - 10 generations/종
- `style_image_url` = `dos2_00` (Divinity: Original Sin 2), `style_copy` = `["color_palette", "shading"]`.
- 프롬프트에서 `forest` 를 뺐다 (배경 방지). 투명 배경으로 깨끗하게 나온다.
- 종: warrior, archer, mage, healer, guardian, imp, imp_archer, imp_shaman (8종 x 16 = 128장)
- 결과: `pro_dos2_00/<종>/c0..c15.png`, 한눈에: `sheet_<종>.png`
- 프롬프트는 `docs/REFERENCE_BRIEF.md` 7장 초안 + `compact readable pixel art, hard pixel edges, 1px dark outline, simple discrete value steps, light from top-left`

## 방법 3: 선택한 앵커 후보를 스타일 입력으로 (create_image_pro, 64x64, 16장/종, seed 1) - 10 generations/종
사용자 지시(2026-11-02): archer c13 과 일관되게 warrior, mage, healer. imp_archer c12 와 일관되게 imp, imp_shaman. guardian 은 방법 2 의 c13 으로 확정.
- `style_image_url` = 방법 2 결과의 다운로드 URL (archer: job 6e335762 index 13, imp_archer: job 1279b37f index 12), `style_copy` = 4가지 전부 (외부 스크린샷이 아니라 우리 후보라서 규칙 13 대상 아님).
- 종: warrior(job d2a88eb3), mage(279227b1), healer(0b8f9b38), imp(ebba1f19), imp_shaman(435070f3)
- 결과: `round2_anchor_style/<종>/c0..c15.png`, 한눈에: `sheet2_<종>.png`
- 주의: imp_archer c12 는 방법 2 의 16장 중 평균 밝기가 가장 낮다 (0.10, 나머지 0.12~0.20). 그 결과 imp, imp_shaman 후보도 매우 어둡다 (평균 0.10, 0.14). 의도한 후보가 맞는지 사용자 확인 필요.

## 방법 4: imp_archer c12 를 밝게 보정해서 스타일 입력으로 (imp, imp_shaman 재생성) - 10 generations/종
사용자 지시(2026-11-02): c12 가 원하는 느낌보다 어두우니 고른 이미지에서 좀 더 밝은 색으로.
- `imp_archer_c12_bright.png`: c12 의 RGB 에 x1.8 배율(색조, 채도 유지, 알파 그대로). 평균 밝기 0.104 -> 0.187. 감마 보정은 윤곽선이 연해지고 초록빛이 돌아 폐기 (`imp_archer_c12_x1.5/1.8/2.1.png`, `compare_imp_*.png` 는 비교용).
- 업로드한 `upload:2aed0f6d-...` 를 `style_image_url` 로, `style_copy` 4가지 전부.
- 종: imp(job d72845df), imp_shaman(6cc36f94). 결과: `round3_bright_imp/<종>/c0..c15.png`, `sheet3_<종>.png`
- 평균 밝기: imp 0.167, imp_shaman 0.190 (방법 3: 0.10, 0.14).

## 비용
- 방법 1: 16, 방법 2: 80 (8종). 합계 96 generations.

## 다음
1. 사람이 `sheet_<종>.png` 에서 종마다 후보를 고른다 (일관된 5~8장 -> `style/anchors/character/`).
2. 사람이 팔레트, 바이블 초안 표시 제거 (Phase 0).
3. 앵커 확정 후 `style_fingerprint.py build`, 이후 `/make-asset`.
