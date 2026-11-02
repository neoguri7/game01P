# 드래프트 1차 재개: 에셋 레인과 코드 레인 분리

`BRIEF.md` 를 먼저 읽는다. 드래프트 규칙, 레퍼런스 규칙(규칙 13), 아트 공통 규칙은 그대로다.
**이 문서는 BRIEF 의 "파일 소유권", "빌드", "PixelLab 예산" 을 대체한다.**

바뀐 점: 지난번 세 트랙은 저마다 생성과 코드를 함께 맡아 서로를 기다렸다. 이제 생성과 코드를 레인으로 나누고,
둘 사이를 `assets/art/asset_contract.json` 이 잇는다. 계약의 34개 파일은 최종 크기의 플레이스홀더(자홍색)로 이미 있고
게임은 그 상태로 부팅된다. 코드는 그림을 기다리지 않고, 그림은 코드를 기다리지 않는다.

기준 커밋: `832fc7d`. 작업 트리는 깨끗하다. **레인은 커밋하지 않는다.** git 으로 작업 트리를 바꾸지 않는다. 커밋은 통합 담당이 한다.

## 레인과 소유

| 레인 | 소유 (이것만 쓴다) |
|---|---|
| `anim` (생성) | `assets/art/anim/**`, `assets/anim/clips.json`(`build_sheets.py` 생성물, 손으로 고치지 않는다), `tools/anim/**`, `draft1/anim/**` |
| `art` (생성) | `assets/art/env/**`, `assets/art/ui/**`, `assets/art/asset_contract.json`(`--stamp` 로만), `draft1/env/**`, `draft1/ui/**` |
| `code` | `src/**`, `CMakeLists.txt`, `assets/data/ui_layout.json`, `assets/data/battlefield.json` 의 표현 필드, `assets/data/scene_*.json`, `assets/data/battle_anim.json` |

- `assets/art/*.png` 의 유닛 앵커 8장은 아무도 바꾸지 않는다. 게임 규칙, 수치, `tests/`, `src/gameplay/` 로직도 바꾸지 않는다.
- `tools/shots/**`, `tools/check_asset_contract.py` 는 통합 담당 소유다. 문제가 있으면 고치지 말고 보고한다.
- 계약을 바꿔야 하면(크기, 앵커, 파일 추가) 고치지 말고 보고한다. 소유 밖 변경이 필요할 때도 같다.
- 게임을 실행하고 캡처하는 레인은 `code` 뿐이다. 캡처가 저장 파일을 덮어쓰기 때문이다.

## PixelLab

- 동시 작업 제한 8개를 슬롯으로 나눈다: `anim` 최대 4, `art` 최대 4. 슬롯을 넘겨 보내지 않는다. 제한 오류는 실패가 아니라 초과 신호다.
- 상한: `anim` 150, `art` 400. 시작 시 잔량 1,299회. 시작과 끝에 `get_balance` 를 `LOG.md` 에 적는다. 다른 레인도 같은 잔량을 쓰므로 차이가 곧 내 소비는 아니다.
- 유료 크레딧 $10 은 쓰지 않는다. 잔량이 100회 아래면 생성을 멈춘다. 한 에셋 재시도는 1회까지다.
- 새로 뽑기 전에 이미 있는 것을 쓴다. 이미 낸 호출은 `draft1/<트랙>/LOG_recovered.jsonl` 에 프롬프트와 job id 가 있다.

## anim 레인

`build_sheets.py` 는 frame 0 이 현재 앵커와 다른 잡을 거부한다. 앵커는 `assets/art/<unit>.png` 이다.
idle 8개는 이미 교체됐다. 남은 32클립 중 아래 10개는 frame 0 검증을 통과한 완료 잡이니 **프레임을 눈으로 보고** 쓴다.

| 유닛 | walk | attack |
|---|---|---|
| warrior | `f3002f91-549a-4c2d-826a-9cf7e1771cc6` | `389bd49d-1615-4ce0-9543-333aa33ea2a8` |
| archer | `fa022852-4468-4bac-8896-68a4fd511e49` | `c68c0eec-2b2f-4446-835d-6dd8d53e09c3` |
| mage | 생성 필요 | `ec58dbfe-8327-4452-83bb-7e16f4f92c33` |
| healer | 생성 필요 | `a51a4569-21b8-40f6-8f86-9519a52e5593` |
| guardian | 생성 필요 | `50ed6867-87e1-4379-a832-a097293e7dbd` |
| imp | 생성 필요 | `1f4732b8-859c-403a-aeb9-0dfa49324894` |
| imp_archer | 생성 필요 | `9e303ea5-a080-4f16-b410-15d2260e1527` |
| imp_shaman | 생성 필요 | `f77447dc-5894-4308-bfdf-dde4563352e4` |

생성할 것: walk 6개, hit 8개, death 8개. 총 22잡. `tools/anim/README.md` 의 방법을 따른다. 프롬프트는 몸의 움직임만 쓴다.
완료 조건: `py tools/anim/build_sheets.py` 가 `--allow-stale` 없이 성공하고, `py tools/check_asset_contract.py` 의 stale 이 0 이다.
공격의 타격 프레임이 바뀌면 `battle_anim.json` 을 고치지 말고 `draft1/anim/impact_frames.json` 에 `{"char.warrior": 4, ...}` 로 적는다. 통합 담당이 넣는다.

## art 레인

계약의 34개 행이 작업 목록이다. 경로, 크기, 알파, 앵커는 계약을 따른다.
파일을 바꾼 뒤 `py tools/check_asset_contract.py --stamp <path> --status generated --job <id>` 로 찍는다. 원본이 로컬 파일이면 `--image <파일>` 을 쓴다.
쓰기는 임시 파일에 한 뒤 `os.replace` 로 바꾼다. 빌드가 도중의 파일을 읽지 않게 하기 위해서다.

순서: 있는 것 재사용 → 바닥, 벽, 소품 → HUD, 타이틀, 스킬 아이콘.

재사용 후보 (job id 는 `LOG_recovered.jsonl`, 갤러리 id 는 `list_images`):

- 바닥: `b5d97ac9`(512x256, 갤러리 `d8c953af`). 이음새 없이 타일링되는지와 `max_value_spread` 0.12 를 통과하는지 확인한다. 안 되면 값 범위를 눌러 맞춘다(재생성보다 먼저).
- 벽 `wall1_*`, 아치 `arch1_*`: `draft1/env/raw`. 벽은 네 후보 중 셋을 고른다.
- 필라 `77dacd08`, 화로 `36ec73de`, 크리스탈 `86848cfc`(각 64x96, 9후보), 고목 `061306b0`(128x160, 4후보), 절벽 `32818fd7`(128x128, 4후보).
- HUD: `t_hotbar_b`(420x84), `t_panel_pixen`(256x128), `t_button`(136x56), `101bd0d7`(72x72), `4adcde22`(320x32), `3767f6da`(460x120).
- 스킬 아이콘: `a7ea3a10`(48x48, 25후보), `t_cleave`.

새로 필요한 것: `decal_*`, `tile_*`, `brazier_anim`(프레임 0 은 `brazier.png` 와 같아야 한다. 불꽃만 움직인다), `title_bg`, 남은 아이콘.
`draft1/env/raw/wang_*` 은 밝은 초록 체크 무늬라 쓰지 않는다.

레퍼런스는 `create_image_pro` 의 `style_image_url` + `style_copy: ["color_palette", "shading"]` 로만 쓴다. BRIEF 의 규칙 13 그대로다.

## code 레인

빌드 폴더는 `out/build/draft-code` 이다. `out/build/windows-msvc-release` 는 건드리지 않는다.

```
cmake --preset windows-msvc-release -B out/build/draft-code
cmake --build out/build/draft-code --config Release --target game01P
py tools/shots/capture.py code --exe out/build/draft-code/Release/game01P.exe
```

CMake 는 재링크할 때만 에셋을 복사한다. `capture.py` 가 매번 직접 복사하고 내용으로 대조하니 캡처는 항상 최신 그림으로 찍힌다. exe 가 `src/` 보다 오래되면 실패한다.

할 일 (BRIEF 의 격차 2~5):

1. **장면 구도.** 셀마다 깐 바닥을 `art/env/ground_a` 타일링으로 바꾸고 데칼을 얹는다. 그리드 밖에 `assets/data/scene_forest.json` 로 벽, 입구 아치, 화로, 크리스탈, 절벽, 고목을 배치한다. 뒤쪽 두 변은 높게, 앞쪽 두 변은 낮게 해서 유닛을 가리지 않는다. 격자 안 Wall 칸은 `tile_blocked` 위에 필라나 벽을 세운다.
2. **빛.** 화로(주황)와 크리스탈(청록)에 빛 웅덩이를 그리고 그 밖은 어둡게 눌러 명도 대비를 만든다. 화로는 `brazier_anim` 을 재생한다. 지금의 고른 청록 빛과 포자 노이즈는 줄인다.
3. **세계를 화면 끝까지.** 전체 화면 프레임을 없앤다(`bands` 0, 프레임 그림 미사용). UI 는 떠 있는 작은 덩어리로: 왼쪽 아래 파티, 가운데 아래 스킬 바, 위 가운데 턴 순서, 오른쪽 아래 턴 종료. 전투 기록은 접거나 줄인다. `art/ui/hud_*` 를 쓴다. 스킬 아이콘은 `skill` 필드로 찾는다.
4. **타이틀.** `art/ui/title_bg` 위에 `title_logo` 를 놓고 메뉴를 얹는다. 플레이스홀더여도 레이아웃을 완성한다.

마우스 피킹은 렌더와 같은 `FIsoView` 를 쓰므로 뷰포트를 바꾸면 함께 맞는지 확인한다. 새 그림의 로드는 `FDungeonArt` 에 넣되 크기를 계약과 대조한다.

완료 조건: 빌드 성공, `ctest` 또는 기존 검증 경로 통과, `capture.py` 로 찍은 화면을 직접 눈으로 본다. 플레이스홀더가 자홍색으로 보이는 것은 정상이다.

## 합격선 (통합 담당이 판정, `capture.py` 가 출력)

| 게이트 | 합격 | 현재 기준선 |
|---|---|---|
| 평균 밝기 (battle_start, battle_auto_b) | 0.13~0.20 | 0.14, 0.15 |
| 상위 5% 밝기 | 0.44 이상 | 0.31, 0.35 FAIL |
| 암부 비율 | 0.35 이하 | 0.40, 0.40 FAIL |
| 채도 | 0.30~0.40 | 0.36, 0.37 |
| HUD 면적 | 0.20 이하 | 0.41 FAIL |
| 계약 `--strict` | 플레이스홀더 0, stale 0 | 34, 32 FAIL |

## 최종 보고

한국어로 짧게. 바꾼 파일, 소비한 generation, 검증 결과, 다른 레인이나 통합 담당에게 필요한 일, 아쉬운 그림과 이유.
`LOG.md` 는 `draft1/<레인>/` 에 쓴다 (art 레인은 `draft1/env/`, `draft1/ui/` 각각). 토큰은 어디에도 적지 않는다.
