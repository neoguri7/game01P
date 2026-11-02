# anim 레인 기록 (드래프트 1차 재개)

기준: `BRIEF.md`, `LANES.md` 의 anim 레인, `tools/anim/README.md`.
도구는 전부 `animate_image_pixminimax`, 64x64, `direction: south`, `view: low top-down`, `no_background` 기본값(true).
앵커는 `assets/art/<unit>.png`. 이번 세션에서 다시 올린 upload ref 를 썼다.

## 잔량

| 시점 | generations_remaining | 메모 |
|---|---|---|
| 시작 | 1,299 | 크레딧 $10 은 쓰지 않음 |
| 끝 | 1,081 | 크레딧 $10 그대로. art 레인 소비 포함 |

## 재사용 앵커 ref (이번 세션, 3일 유지)

| 유닛 | ref |
|---|---|
| warrior | `upload:54d15a3a-5b82-4f71-8a2e-e00c0b43a1b2` |
| archer | `upload:99f2af1f-8f97-40c2-8683-425f75a7b130` |
| mage | `upload:36fbb0c8-545f-40ca-9e96-5d8e1c74940b` |
| healer | `upload:3c302997-0f9a-4061-8085-f0b2a5f298f0` |
| guardian | `upload:4b9f7bcf-eeba-4ba2-90d9-006e40c15218` |
| imp | `upload:e7874b68-30a4-4c78-9016-48e7e01a678f` |
| imp_archer | `upload:ea9e9ff3-26b1-4e91-accf-2d8f92bfce3e` |
| imp_shaman | `upload:9e0692ef-5a84-4b1a-8602-bda859d1e7f1` |

## 캡션 (subject_description / initial_pose)

`LOG_recovered.jsonl` 의 것을 유닛별로 그대로 썼다.

- warrior: a knight in dark steel plate armour and a dark red tabard, with a short sword and a large grey kite shield / standing upright, sword held low in the right hand, kite shield on the left arm
- archer: a hooded ranger in a dark olive cloak and leather armour, with a tall longbow and a quiver on the back, face hidden in shadow / standing upright, tall longbow held vertically in one hand at the side
- mage: a robed wizard in a deep indigo robe and a wide pointed hat, with a twisted wooden staff, face hidden in shadow / standing upright, twisted staff held vertically in one hand at the side
- healer: a healer in a cream hooded robe with a leather satchel, a small green orb floating above one open hand, face hidden in shadow / standing upright, one hand held out with the small green orb above the palm
- guardian: a huge stone golem armoured in mossy grey rock plates, gripping a massive iron-studded stone warhammer / standing with legs apart, the warhammer held upright beside the body with its head at the top
- imp: a small hunched goblin imp with dark brown skin, long pointed ears, a ragged loincloth and clawed hands / crouched with long arms hanging at the sides and feet apart
- imp_archer: a small hunched goblin imp with dark brown skin, long pointed ears and a ragged loincloth, holding a small curved wooden bow / crouched with long arms low and feet apart, the bow held low at one side
- imp_shaman: a small hunched hooded goblin shaman in a tattered brown robe, with a wooden staff topped by a skull and a small green flame / standing hunched, the skull-topped staff held upright in one hand

seed 규칙: 유닛 기준값(warrior 100, archer 200, mage 300, healer 400, guardian 500, imp 600, imp_archer 700, imp_shaman 800) + idle 0, attack 1, walk 2, hit 3, death 4. 재시도는 +10.

## 이번 세션 잡

| 클립 | job id | seed | frames | 비용 | 프롬프트 |
|---|---|---|---|---|---|
| mage.walk | `a4c8a2b2-ae60-45d3-9024-c95bcee4761c` | 302 | 8 | 1 | Walking in place toward the viewer, a steady looping stride: the legs alternate stepping under the robe, the robe hem sways, the staff stays held upright at the side. The body stays in the same spot and ends back in the starting pose. Body motion only. |
| healer.walk | `e8b8de39-7fd8-4baf-8117-abe33a225f06` | 402 | 8 | 1 | Walking in place toward the viewer, a steady looping stride: the legs alternate stepping under the robe, the robe hem sways, the hand with the small orb stays out at the side. The body stays in the same spot and ends back in the starting pose. Body motion only. |
| guardian.walk | `08c089fe-f027-4cf5-bc3d-6e3cc766396d` | 502 | 8 | 1 | Walking in place toward the viewer, a heavy steady looping stomp: the thick legs alternate stepping, the huge shoulders rock slightly, the warhammer stays held upright beside the body. The body stays in the same spot and ends back in the starting pose. Body motion only. |
| imp.walk | `0a9568fc-e546-4f71-88b0-09bffa769611` | 602 | 8 | 1 | Walking in place toward the viewer, a quick looping scuttle: the legs alternate stepping, the long arms swing a little, the hunched body bobs slightly. The body stays in the same spot and ends back in the starting pose. Body motion only. |
| imp_archer.walk | `c17ba225-1b25-4671-ad7a-ad1d05bad07c` | 702 | 8 | 1 | Walking in place toward the viewer, a quick looping scuttle: the legs alternate stepping, the free arm swings a little, the small bow stays held low at one side, the hunched body bobs slightly. The body stays in the same spot and ends back in the starting pose. Body motion only. |
| imp_shaman.walk | `bbdb58f3-54ae-4f3e-98a4-b6e39bec1b5b` | 802 | 8 | 1 | Walking in place toward the viewer, a slow looping shuffle: the legs alternate stepping under the tattered robe, the robe hem sways, the hunched body bobs slightly, the skull-topped staff stays held upright and the green flame keeps its size. The body stays in the same spot and ends back in the starting pose. Body motion only. |
| warrior.hit | `35bcb2f6-d1c5-4170-82b1-ea03a2d08be2` | 103 | 4 | 1 | The torso jerks backward and the head dips as if struck, the shield arm tightens against the chest, then the body recovers to the starting pose. Feet stay planted. Body motion only. |
| archer.hit | `c66736f4-b8f1-49c6-bdf3-0100b64f50bb` | 203 | 4 | 1 | The torso jerks backward and the hooded head dips as if struck, the free arm recoils, the longbow stays held upright, then the body recovers to the starting pose. Feet stay planted. Body motion only. |
| mage.hit | `71572c0a-9cfc-457a-915d-f6546884b43b` | 303 | 4 | 1 | The whole upper body snaps sharply backward as if struck, the head with the pointed hat tips back, the free arm flies out, the staff stays in the hand, then the body recovers to the starting pose. Feet stay planted. Body motion only. |
| healer.hit | `4e7f95ee-18e0-44d0-84ce-481b4afe2924` | 403 | 4 | 1 | The whole upper body snaps sharply backward as if struck, the hooded head tips back, the open hand with the small orb jerks in toward the chest, then the body recovers to the starting pose. The orb keeps its size. Feet stay planted. Body motion only. |
| guardian.hit | `71c42718-e0ce-48e2-9568-79820ab275d2` | 503 | 4 | 1 | The huge torso rocks backward as if struck, the shoulders jolt, the head dips, the warhammer stays gripped upright, then the body settles back to the starting pose. Feet stay planted. Body motion only. |
| imp.hit | `08340a0d-a8bf-4dbb-88ac-8bc683476dc3` | 603 | 4 | 1 | The whole hunched body snaps sharply backward as if struck, the head jerks back, the long arms fly out to the sides, then the body drops back to the starting crouch. Feet stay planted. Body motion only. |
| imp_archer.hit | `a2f9e23d-121e-46bc-87c0-b4a988d7b230` | 703 | 4 | 1 | The hunched upper body jerks backward as if struck and the head tilts up, the arms lift a little, the bow stays in the hand, the knees stay bent and the crouch stays low, then the body returns to the starting pose. Feet stay planted. Body motion only. |
| imp_shaman.hit | `9ab5058c-0cfb-4b47-9d9f-42de9aba4bf4` | 803 | 4 | 1 | The hunched upper body jerks backward as if struck and the hooded head tilts up, the free arm lifts a little, the staff stays held upright and the green flame keeps its size, then the body returns to the starting pose. Feet stay planted. Body motion only. |
| warrior.death | `fbebdbe8-8360-4096-9a7f-d25863e8af2b` | 104 | 8 | 1 | The knees buckle and the body sags, the armoured torso topples slowly to one side, and the body collapses onto the ground, the sword and shield going down with it, ending lying still on the ground. Body motion only. |
| archer.death | `35d2c87b-02ac-49ea-9d0e-b130c88ee190` | 204 | 8 | 1 | The knees buckle and the body sags, the cloaked torso topples slowly to one side, and the body collapses onto the ground, the longbow falling with it, ending lying still on the ground. Body motion only. |
| mage.death | `1344be05-f35d-48e3-a755-4797207088df` | 304 | 8 | 1 | The knees buckle and the body sags, the robed torso topples slowly to one side, and the body collapses onto the ground, the staff falling with it, ending lying still on the ground. Body motion only. |
| healer.death | `e01d927b-655b-4128-832a-069aa9bb9e54` | 404 | 8 | 1 | The knees buckle and the body sags, the robed torso topples slowly to one side, and the body collapses onto the ground, the open hand dropping with it, ending lying still on the ground. Body motion only. |
| guardian.death | `18d7970c-ae84-4e3e-ae3f-d4adf2f2afa5` | 504 | 8 | 1 | The thick legs give way and the huge body sags, the heavy torso topples slowly to one side, and the body crashes down onto the ground, the warhammer falling with it, ending lying still on the ground. Body motion only. |
| imp.death | `7dc50af3-df7a-4c74-8b92-c34a73e37955` | 604 | 8 | 1 | The knees buckle and the crouched body sags, the hunched torso topples slowly to one side, and the body collapses onto the ground with the long arms sprawled, ending lying still on the ground. Body motion only. |
| imp_archer.death | `cb58306e-c6f2-4a76-ae36-6915c238aa89` | 704 | 8 | 1 | The knees buckle and the crouched body sags, the hunched torso topples slowly to one side, and the body collapses onto the ground, the small bow falling with it, ending lying still on the ground. Body motion only. |
| imp_shaman.death | `4ce42767-2102-470a-b9fc-e4ea7de5e0ef` | 804 | 8 | 1 | The knees buckle and the hunched body sags, the robed torso topples slowly to one side, and the body collapses onto the ground, the skull-topped staff falling with it, ending lying still on the ground. Body motion only. |

## 재시도 잡 (클립당 1회)

`last` 가 yes 인 잡은 `last_frame_url` 에도 같은 앵커를 넣어 끝 포즈를 고정했다. 앵커로 돌아오지 않던 문제에 효과가 있었다.

| 클립 | job id | seed | frames | last | 비용 | 채택 | 프롬프트 |
|---|---|---|---|---|---|---|---|
| imp.attack | `63d5af22-b46b-4edd-a10f-70711f4499d7` | 611 | 8 | yes | 1 | 채택 | The imp rears back with one clawed arm raised high, then lurches forward and brings the arm down in front of its body until the claws are low near the ground, then returns to the starting crouch. No motion trail, only the body moves. Feet stay planted. Body motion only. |
| imp_shaman.attack | `b63675dd-5afb-4d4b-b69e-02605f1444e8` | 811 | 8 | yes | 1 | 버림 (프레임 2~4 불꽃 사라짐, 5~7 바닥 잔점) | The arm lifts the skull-topped staff straight up, then drives it straight down into the ground beside the body with the hunched body leaning into it, then returns to the starting pose. The staff stays vertical and close to the body, the green flame keeps its size. Feet stay planted. Body motion only. |
| mage.hit | `f49cf1ee-be3a-426c-a16e-60ecb8279d57` | 313 | 4 | yes | 1 | 채택 | The upper body leans back as if struck and the head dips, the pointed hat stays on the head and keeps its shape, the free arm lifts a little, the staff stays in the hand, then the body returns to the starting pose. Feet stay planted. Body motion only. |
| guardian.hit | `60236635-31b6-458a-a8d9-33672c91c981` | 513 | 4 | yes | 1 | 채택 | The whole huge body rocks clearly backward as if hit by a heavy blow, the torso tilts away and the shoulders jolt upward, the warhammer stays gripped upright, then the body rocks forward back to the starting pose. Nothing breaks off. Feet stay planted. Body motion only. |
| imp.hit | `5e68e668-fe71-4820-af76-d9bb0f60f6b3` | 613 | 4 | yes | 1 | 채택 | The hunched upper body jerks backward as if struck and the head tilts up, the knees stay bent and the crouch stays low, the arms lift a little, then the body drops back to the starting crouch. Feet stay planted. Body motion only. |
| archer.walk | `6a0b2dd1-77f9-48bd-9b0c-2966d758f1c1` | 212 | 8 | yes | 1 | 채택 | Walking in place toward the viewer, a steady looping stride: the legs alternate stepping, the cloak sways slightly, the longbow stays held upright at the side and stays rigid, it does not bend or wave. The quiver stays on the back. The body stays in the same spot and ends back in the starting pose. Body motion only. |
| imp_archer.hit | `6074532f-a044-49e3-8641-362eda1a2b5b` | 713 | 4 | yes | 1 | 채택 | The hunched upper body jerks backward as if struck and the head tilts up, the knees stay bent and the crouch stays low, the arms lift a little, the bow stays in the hand, then the body drops back to the starting crouch. Feet stay planted. Body motion only. |
| imp_shaman.hit | `fe82b4db-7156-4d4b-a7b5-5375119ca3d9` | 813 | 4 | yes | 1 | 채택 | The hunched upper body jerks backward as if struck and the hooded head tilts up, the free arm lifts a little, the staff stays held upright and the green flame keeps its size, then the body drops back to the starting pose. Feet stay planted. Body motion only. |
| imp_shaman.death | `b0763c14-3bcb-453b-864a-d76eb7a2b653` | 814 | 8 | no | 1 | 채택 | The knees buckle and the hunched body sags, the robed torso topples slowly to one side, and the body collapses onto the ground with the staff lying beside it, ending lying still on the ground. The green flame on the staff shrinks and goes out as the body falls. No sparks. Body motion only. |
| warrior.hit | `28209d11-4aea-41ec-acac-7ac14b88836b` | 113 | 4 | yes | 1 | 채택 | The upper body leans clearly backward as if struck and the head dips, the shoulders jolt, the shield stays on the arm and the sword stays in the hand, then the body returns to the starting pose. Feet stay planted. Body motion only. |
| archer.hit | `60a9bcdb-1ac9-45d8-9ac6-6aa17decf35f` | 213 | 4 | yes | 1 | 버림 (프레임 2 활이 끊김) | The upper body leans clearly backward as if struck and the hooded head dips, the free arm lifts a little, the longbow stays upright and rigid in the hand, then the body returns to the starting pose. Feet stay planted. Body motion only. |

## 최종 프레임 선택과 이유 (`assets/art/anim/sources.json`)

표에 없는 클립은 잡의 전 프레임을 쓴다. 다른 잡을 쓰는 클립의 job id 는 위 표와 같다.

| 클립 | frames | 이유 |
|---|---|---|
| warrior.attack | 0,1,2,3,5,6,7,8 | 3, 4 가 같은 자세. 내리침(잡 5)이 클립 4 로 유지됨 |
| archer.walk | 0..7 | 끝 고정이라 8 은 0 과 거의 같음. 루프 중복 제거 |
| archer.attack | 0,1,3,4,5,7,8 | 2, 6 은 중복. 놓는 순간(잡 7)이 클립 5 로 유지됨 |
| mage.death, imp.death, imp_archer.death, imp_shaman.death | 0,2,4,5,6,7,8 | 앞쪽 프레임이 거의 정지 상태라 넘어지기까지의 대기를 줄임 |
| healer.hit | 0,2,3,4 | 프레임 1 에 오브 궤적이 그려짐 |
| imp.attack | 0,2,3,5,6,7,8 | 프레임 4 에 궤적 잔선. 내리침(잡 5)이 클립 3 으로 유지됨 |
| imp_archer.attack | 0,1,2,3,5,6,7,8 | 놓는 순간(잡 6)이 클립 5 로 유지됨 |
| imp_shaman.attack | 0,1,2,3,6,7,8 | 원본 잡 사용. 4, 5 는 해골이 오른쪽 끝에서 20px 이상 잘림. 찌르기(잡 6)가 클립 4 로 유지됨 |
| guardian.attack | 0,1,2,3,4,5,6,8 | 프레임 7 망치가 몸통에 뭉개짐 |

## 타격 프레임

`impact_frame` 은 `frames` 로 추린 뒤의 클립 인덱스다 (`FBattleAnim.cpp`, `SecondsBeforeFrame`).
바뀌는 것은 guardian 하나다: 망치가 땅에 닿는 잡 프레임 4 가 클립 4 이므로 5 → 4. `draft1/anim/impact_frames.json` 에 적었다. 나머지 7개는 프레임 선택으로 기존 값을 유지했다.

## 검증

- `py tools/anim/build_sheets.py` (`--allow-stale` 없이): 성공, 53 시트와 `clips.json` 기록.
- `py tools/check_asset_contract.py`: `unit clips not made from the anchors: 0`. 종료 코드 1 은 `art/ui`, `art/env` 34행의 `file changed without --stamp` 때문이며 art 레인 작업 중인 파일이다. anim 소유 파일의 FAIL 은 없다.
- `py -m unittest discover -s tools/anim -p "test_*.py"`: 11개 통과.
- 변경 범위: `git status` 기준 `assets/anim/clips.json`, `assets/art/anim/sources.json`, `assets/art/anim/*_{walk,attack,hit,death}.png` 32장.
- 콘택트 시트 전수 확인: `draft1/anim/sheets/`. 수치 검사: `draft1/anim/contact.py`, `draft1/anim/qa_frames.py`.
- 게임은 실행하지 않았다 (code 레인만 실행).

## 아쉬운 클립

- archer.walk, archer.hit: 활이 조금씩 물결치고 망토 안쪽이 어두워진다. 재시도 후에도 남음.
- archer.attack: 조준 이후 망토가 큰 검은 덩어리가 되고, 끝이 시작 자세로 돌아오지 않는다.
- imp_shaman.attack: 찌르기(프레임 6)에서 해골이 오른쪽 끝에 12px 닿아 조금 잘린다. 깊은 찌르기는 잘려서 뺐기 때문에 동작도 작다.
- imp_archer.attack: 활을 쏜 뒤 선 자세로 끝나 idle 크라우치로 튄다.
- warrior.hit: 재시도 후에도 움직임이 작다.
- healer.attack: 손과 오브의 움직임이 작아 공격으로 약하게 읽힌다.
- mage.attack: 프레임 5~7 지팡이 끝이 오른쪽 끝에 닿아 몇 픽셀 잘린다.
- mage.death: 끝 프레임의 모자가 검은 고리처럼 보인다.
- imp_archer.death: 끝 프레임이 땅에 완전히 눕지 않아 살짝 떠 보인다.
- imp_shaman.death: 불꽃 잔점 1~2px 이 남는다. 쓰러지는 불꽃으로 읽혀서 유지했다.
- 임프 3종 hit: 끝 고정으로 돌아오지만 가운데 프레임은 크라우치보다 크게 일어선다.

## 소비

- 이번 세션 anim 잡: 33개, 33 generations (본 잡 22, 재시도 11). 상한 150 이내.
- 잔량 변화 1,299 → 1,081 에는 art 레인 소비가 포함된다.

## 리뷰 메모 (콘택트 시트: `draft1/anim/sheets/`, 도구: `draft1/anim/contact.py`)

완료 잡 10개는 전부 frame 0 == 앵커 확인.

- warrior.walk: 양호. 전 프레임 유지.
- archer.walk: 프레임 2 화살통 깨짐, 전 프레임 활이 S자로 휨.
- warrior.attack: 3, 4 거의 같은 자세. 내리침은 프레임 5.
- archer.attack: 4~6 조준 유지, 7 에서 놓음.
- mage.attack: 양호. 프레임 4 가 찌르기.
- healer.attack: 움직임이 작지만 깨진 곳 없음.
- guardian.attack: 프레임 7 망치 머리가 몸통에 뭉개짐. 내리침은 프레임 4.
- imp.attack: 프레임 7 에 궤적 호, 8 에 잔여 호가 그려짐 (이펙트). 재시도 대상.
- imp_archer.attack: 프레임 6 에서 놓음.
- imp_shaman.attack: 프레임 4, 5 에서 해골과 불꽃이 셀 오른쪽 끝에서 잘림. 재시도 대상.
- mage/healer/guardian/imp.walk: 깨진 프레임 없음. 전 프레임 유지.
