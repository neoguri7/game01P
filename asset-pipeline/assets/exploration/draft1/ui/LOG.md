# draft1 ui 기록 (art 레인)

날짜: 2026-11-02. 기준 커밋 `832fc7d`. 승인 대상이 아닌 탐색 라운드다.

## 잔량

시작 1,299, 끝 1,081. anim 레인과 공용이다. art 레인 소비 185 중 ui 는 135 (아이콘 110, 타이틀 배경 25). 시작과 끝 값은 `env/LOG.md` 와 같다.

## LANES 와 다른 점

`a7ea3a10` (48x48, 25후보) 은 스킬 아이콘이 아니다. 프롬프트도 결과도 **창 모서리 장식**이다. 그래서 `t_cleave` 를 뺀 아이콘 11개를 새로 생성했다.

`hud_panel` 은 `t_panel_pixen` 대신 `t_hotbar_b` 재질로 만들었다. `t_panel_pixen` 을 먼저 만들어 봤는데 벽돌 프레임이 밝고 두꺼워 `hud_bar` 와 다른 물건처럼 보였다.

## 재사용 (새 호출 없음)

| 계약 행 | 원본 | job | 처리 (`ui/scripts/hud.py`, 크기 변경 없음) |
|---|---|---|---|
| hud_bar | `t_hotbar_b` | `e99ce0b8-4380-4120-bac2-fa16056cd043` | 왼쪽이 잘려 있어 오른쪽 절반을 거울로 복사. 띠를 위아래로 벌려 안쪽 49px 확보, 안쪽 노이즈를 단색으로 |
| hud_panel | `t_hotbar_b` | 같음 | 모서리 24px, 위 띠를 90도 돌려 옆 레일로, 안쪽 단색 |
| hud_button | `t_button` | `e964558a-aa50-4237-832c-fc47bceb83fb` | 원본이 75x36 으로 작다. 모서리를 옮기고 레일을 이어 134x54 로, 안쪽 단색 |
| hud_portrait | `101bd0d7` | `101bd0d7-df2a-4106-9166-423db49a1148` | 가운데 (15,15)~(56,56) 을 투명으로 |
| hud_turnbar | `4adcde22` | `4adcde22-2469-4515-b607-7c37776c2bc3` | 원본이 간격을 두고 둘로 나뉘어 있다. 양쪽 끝이 있는 오른쪽 조각의 가운데를 이어 318 폭으로 |
| title_logo | `3767f6da` | `3767f6da-00e9-4186-bdc3-9ecb2dad1eec` | 빈 현판에 Cinzel(게임 폰트, OFL)로 "EMBER DUNGEON" 을 안티앨리어싱 없이 찍었다. 3단 색. `ui/scripts/logo.py` |
| skill_cleave | `t_cleave` | `0d5ab2c9-c382-412d-ad1e-2db3cf12ee32` | 그대로 |

`t_panel_uiasset` 은 높이가 맞지 않고, `t_hotbar_a` 는 400x104 라 쓰지 않았다.

## title_bg

도구 create_image_pro, 688x384, `no_background: false`, seed 21201, job `25674198-2f78-4c1b-af4c-8fcb4ec41b10`, 25 generation, 후보 1장.
레퍼런스 `poe_08` (`upload:eb606093-7264-42d7-9fe7-e3913cd92b34`) 을 `style_image_url` 로만 넘겼고 `style_copy` 는 `["color_palette", "shading"]` 이다 (규칙 13). 재시도 없음. 평균 V 0.137.

```
dark fantasy forest ruin, compact readable game pixel art, hard pixel edges, no anti-aliasing, no gradients, simple discrete value steps, light from top-left, restrained desaturated palette. Wide title scene seen from a 3/4 isometric camera: a ruined moss-covered stone temple courtyard at night, a broken stone archway with a black passage in the centre at the back, irregular dark flagstones in the foreground, a tall iron fire brazier on the left casting warm orange light on the stones, a cluster of glowing cyan crystals on the right, twisted dead trees and roots framing both sides, deep darkness at all edges, calm empty upper third, no people, no text
```

## 스킬 아이콘

모두 create_image_pro, 48x48, `no_background: true`, 후보 25장, 호출당 10 generation. 스타일 입력은 우리 `t_cleave` (`upload:f787bc30-fea4-4b0d-82f6-a117a80fb54a`), `style_copy` 기본값 4개. 재시도 없음.

고정 앞부분:

```
compact readable game pixel art, hard pixel edges, no anti-aliasing, simple discrete value steps, light from top-left, restrained desaturated palette. Skill icon,
```

고정 뒷부분:

```
centered, flat icon, single black outline, no background, no frame
```

| 스킬 | job | seed | 고른 후보 | 중간 문구 |
|---|---|---|---|---|
| strike | `072b227c-b03d-425b-b42a-c44912db4c21` | 21101 | 12 | a short iron sword striking downward diagonally, a small white impact spark at the blade tip |
| arrow | `cd613e00-248f-4dfa-9062-01c64190f8c7` | 21102 | 12 | a single wooden arrow with a steel head and pale feather fletching flying diagonally up to the right |
| volley | `0d7451b1-112d-4a0f-bebc-7ea71185f50f` | 21103 | 15 | three wooden arrows falling together diagonally downward in a tight fan, steel heads, pale fletching |
| bolt | `d9080b52-ae5f-4364-9188-f9b9973af67c` | 21104 | 9 | a violet arcane magic bolt, a small bright orb with a short tapering energy tail flying diagonally up to the right |
| fog | `67d47619-43ba-400e-8639-13ac09b91faf` | 21105 | 8 | a low rolling cloud of pale grey-blue fog with swirling curls |
| heal | `7848c0da-da93-4a58-a4be-08407401677a` | 21106 | 5 | a cupped open hand holding a small glowing pale green healing orb with tiny rising sparks |
| buff | `3ba4b9bd-ca2c-4f5a-938a-1cc0ab392004` | 21107 | 5 | a round iron shield with a bronze rim and a bright upward gold chevron on it, a blessing that strengthens an ally |
| debuff | `3f6f0341-87eb-454f-b876-eeac89aba77c` | 21108 | 18 | a cracked small bone skull with sickly green curse drips and a downward arrow, a curse that weakens an enemy |
| lunge | `aa653f27-11af-4993-859f-5a720fdeafc4` | 21109 | 7 | a slim steel spear thrusting forward horizontally to the right, three short white speed lines trailing behind it |
| snipe | `af373392-0447-4aff-87a9-d7314656913f` | 21110 | 24 | a bronze aiming ring crosshair with a single thin arrow passing through its centre, a precise long shot |
| firebolt | `356804b2-a0ff-4c4b-9e1e-150e93c36382` | 21111 | 6 | a blazing orange fireball comet with a yellow core and a short flaming tail flying diagonally up to the right |

스타일 입력의 모션 호가 왼쪽 위 얼룩으로 따라와서 strike 와 debuff 에서 떼어 냈다 (`ui/scripts/icons.py`).

## 확인

아이콘 후보 시트 11장을 모두 봤고, HUD 조각과 아이콘을 타이틀 배경 위에 올려 함께 봤다. 게임은 실행하지 않았다.

## 아쉬운 그림

- `skill_lunge`: 가로 찌르기와 속도선을 요청했지만 25장 모두 대각선 창으로 나왔다. `skill_arrow` 와 실루엣이 가깝다.
- `skill_fog`: 안개보다 바람에 가깝게 읽힌다.
- `hud_button`: 강철 색이 `hud_bar` 의 철과 금보다 밝고 차갑다. 옆 레일에 이어 붙인 자국이 작게 남았다.
- `hud_turnbar`: 구리 장식이 다른 조각의 금색과 조금 다르다. 가운데 이음매가 리벳처럼 보인다.
- `hud_panel`: 위 띠를 돌려 만든 옆 레일은 빛 방향이 위 띠와 맞지 않는다.
