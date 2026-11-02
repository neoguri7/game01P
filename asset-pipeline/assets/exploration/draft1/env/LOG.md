# draft1 env 기록 (art 레인)

날짜: 2026-11-02. 기준 커밋 `832fc7d`. 승인 대상이 아닌 탐색 라운드다.

## 잔량

| 시점 | generations_remaining |
|---|---|
| 시작 | 1,299 |
| 끝 | 1,081 (anim 레인과 공용. 차이 218 중 art 레인 185) |

art 레인 소비: env 50 (인페인트 30, 데칼 20), ui 135. 유료 크레딧 사용 없음.

## 재사용 (새 호출 없음)

| 계약 행 | 고른 후보 | job | 세로 이동 | 이유 |
|---|---|---|---|---|
| wall_a | `wall1_2` | `63d026eb-25b3-45a3-9e7e-df7aa667977e` | +8 | 곧은 벽, 줄 세우기 좋다 |
| wall_b | `wall1_3` | 같음 | +9 | 가운데가 패인 실루엣 |
| wall_c | `wall1_1` | 같음 | +8 | 낮게 무너진 벽. 앞쪽 변용 |
| arch | `arch1_0` | `1396704f-17fc-402c-b121-931724a8d0ec` | +4 | 실루엣과 입구가 가장 잘 읽힌다 |
| cliff | `cliff_0` | `32818fd7-9121-42f5-81c6-81df5055a995` | +9 | 덩어리가 단순하다 |
| pillar | `pillar_7` | `77dacd08-dd81-4a37-8ce4-421050d914b2` | +15 | 밑동 중심이 앵커에 맞고 낮아서 뒤 유닛을 덜 가린다 |
| brazier | `brazier_7` | `36ec73de-9c40-4628-9995-73942b862ed7` | +8 | 게임 크기에서 잘 읽힌다 |
| crystal | `crystal_0` | `86848cfc-7979-4dab-9738-5bf7340737d5` | +5 | 밑동이 작고 석조와 어울린다 |
| tree_dead | `tree_0` | `061306b0-b6c3-49b7-b6f1-586799283675` | +14 | 잡티 픽셀이 없다 (`tree_1`, `tree_3` 은 흰 점이 있다) |
| tile_trap | 타일 12 | `56ed8f60-fd61-4f3d-baa4-620ff4626fff` | 없음 | 가운데 64x32 를 잘라 정확한 다이아몬드로. 돌을 바닥 값으로 맞췄다 |
| tile_blocked | 타일 2 | 같음 | 없음 | 같은 방식. 바닥보다 어둡게 했다 |

모든 픽셀 이동은 정수 평행이동이고 크기 변경은 없다. 벽, 아치, 절벽, 필라 몸통은 값을 0.86 배 해서 유닛 아래로 내렸다. 화로 불꽃과 크리스탈은 그대로다.
스크립트: `env/scripts/place2.py`, `env/scripts/tiles.py`.

`wang_*`, `flag_*` 는 쓰지 않았다.

## ground_a

1. `b5d97ac9-3794-4052-aabe-469c4edf0144` (512x256) 를 받아 `env/raw/ground512.png` 로 저장. V 5%~95% 폭 0.34, 가장자리 잇기 차이 26 (내부 10) 으로 그대로는 쓸 수 없었다.
2. 가운데 다이아몬드를 아이소 격자 주기 (256,128), (256,-128) 로 접어 `ground_T0.png` 를 만들었다 (`ground1.py`).
3. 이음새를 인페인트로 지웠다. 아래 세 건 모두 같은 프롬프트, 256x256 창, 사용자 마스크. 512 는 Tier 2 전용이라 격자 위에서 창을 잘라 보냈다.

프롬프트 전문:

```
worn dark irregular flagstones seen from a 2:1 isometric camera, thin dark mortar joints between stones, patches of damp moss in the joints, stones continue naturally across the area, quiet low contrast, compact game pixel art, hard pixel edges
```

| 도구 | job | seed | 창 중심 | 마스크 | generation |
|---|---|---|---|---|---|
| inpaint_image | `57d56019-56d3-47ac-81ad-8d4723e12cb0` | 21001 | (256,0) | 격자점을 지나는 X 띠 ±13줄 | 10 |
| inpaint_image | `5c957026-a2d1-4db2-940a-28a6b3119a1c` | 21002 | (128,64) | 타원 44x22 | 10 |
| inpaint_image | `6f684fc6-a9b8-4f11-88d1-b8cca58c6cad` | 21003 | (384,64) | 타원 44x22 | 10 |

`no_background: false`. 인페인트가 마스크 밖 픽셀을 바꾸지 않은 것을 확인했다 (변경 0).

4. 팔레트 19색을 항목별로 값 6단으로 눌렀다 (`ground4.py`). 합성색이 생기지 않는다. 약간 차갑게 옮겼고, 끌어올린 줄눈이 붉어지지 않게 어두운 항목의 채도를 낮췄다.

검증 결과: V 5%~95% 폭 0.106 (상한 0.12), 평균 0.170. 2x2 반복을 눈으로 확인했다. 아이소 격자 다이아몬드 합성과 직사각 반복이 픽셀까지 같다 (일치율 1.0). 가장자리 차이는 내부 차이 이하다.

## 데칼

레퍼런스는 우리 벽 `wall1_2` (`upload:9c24577b-ed55-47d3-aa38-38011b3c2a72`), `style_copy` 기본값 4개. 128x64, `no_background: true`, 후보 4장, 호출당 10.

decal_moss. job `ab515768-da9b-4bff-8b8d-392261e3b09e`, seed 21010. 후보 0 사용, 값 0.68 배.

```
compact readable game pixel art, hard pixel edges, no anti-aliasing, no gradients, simple discrete value steps, restrained desaturated palette. Flat patch of dark damp moss lying flat on the ground, seen from a 2:1 isometric camera, irregular organic oval outline wider than tall, spreading fingers of moss at the edges, completely flat with no thickness and no height, no stones, no objects, transparent everywhere else
```

decal_rubble. job `7abdf9f6-a282-4f22-b12f-c94fd1df3634`, seed 21011. 후보 0 사용, 값 0.72 배.

```
compact readable game pixel art, hard pixel edges, no anti-aliasing, no gradients, simple discrete value steps, restrained desaturated palette. Scattered flat debris on the ground seen from a 2:1 isometric camera, small broken stone chips, grit and pebbles spread in a loose irregular oval wider than tall, very low, hugging the ground, no tall stones, no walls, no shadow, transparent everywhere else
```

두 데칼 모두 순흑 외곽선을 어두운 회색으로 올렸다 (`decals.py`).

## brazier_anim

생성 없음. `env/scripts/brazier_anim.py` 가 `brazier.png` 의 불꽃 픽셀을 프레임마다 옮겨 6프레임 띠를 만든다. 프레임 0 은 정지 그림과 같고 몸통은 바뀌지 않는다 (변경 범위 x 26~37, y 13~34). 처음부터 불꽃을 그리거나 `animate_image` 를 쓰는 안은 시도하지 않았다.

## 확인

`env/scripts/scene.py` 로 2x2 바닥 위에 소품, 데칼, 타일, 앵커 8종을 게임 픽셀 그대로 놓고 봤다. 게임은 실행하지 않았다.

## 아쉬운 그림

- `ground_a`: 격자 주기가 8칸이라 12x12 격자 안에서 금 간 판석이 두 번 보인다.
- `wall_*`, `cliff`: 앵커가 벽체 위에 놓이지 않는다. 벽이 대각선이라 앵커 x=64 에서 앞 밑변이 y≈108 (벽), y≈115 (절벽) 이다. 앵커 y=120 까지 내리면 왼쪽 아래가 잘린다. 계약은 고치지 않았다.
- `wall_*`, `cliff` 는 왼쪽 아래에서 오른쪽 위로 가는 한 방향뿐이다. 반대 변은 뒤집어야 하고 그러면 광원이 어긋난다.
- `tree_dead`: 줄기 밑동이 앵커보다 약 20px 위다. 뿌리 끝이 캔버스 아래까지 닿아 더 내릴 수 없다.
- `tile_blocked`: 파란 회색 원본을 바닥 색으로 옮겨서 세부가 적다.
