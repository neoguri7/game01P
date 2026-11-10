# 설계 뼈대와 근거 (improve-loop 기록)

상태: 3라운드 개정(2026-11-11). 이 문서는 Ember Dungeon의 뼈대를 **근본적인 층부터** 적고, 층마다 "명작이 실제로 한 것 → 지금 우리 게임(확정 결정 포함) → 판정 → 다음에 확인할 것"을 근거와 함께 남긴다. 순서는 **뼈대 → 체계적 확장 → 밸런스(맨 마지막)**. 수치는 다루지 않는다. 구현 설계는 각 WP 스펙이 맡는다. 확정 결정(`decisions.md`)을 바꾸는 것은 §3 "사용자 판단"에만 올린다.

근거 등급: **[원문]** 저장한 원문과 대조됨 · **[보관본]** 웹 보관본 원문과 대조됨 · **[요약]** 요약 도구를 거침(결정의 근거로 쓰기 전 대조 필요) · **[2차]** 위키·기사·리뷰 · **[미확인]** 1차 자료 없음. 조사 원본: `docs/digests/r1/`, `r2/`, `r3/`(로컬, 추적 안 함).

읽는 법: "규칙"이라고 쓴 것은 **둘 이상의 게임에서 원문으로 확인된 것**뿐이다. 한 게임의 선택은 "사례"라고 쓴다. 명작만 조사했으므로 생존 편향이 있다 — 실패·수정 사례는 따로 표시한다.

## 0. 판단 기준 (기존 확정)
기대의 극대화 + 적절한 변수성. 보상은 불확실하게, 손실은 읽히게. 입력은 공개, 결과는 연출. 무작위는 "언제·어디서"에. (`expansion-plan.md` §1) · 리서치와 현재 설계가 충돌하면 리서치의 신뢰도로 판단한다(`decisions.md` 231).

조사가 이 기준에 더한 것:
- 플레이어가 이미 정한 행동의 결과를 **깎는** 무작위가 불만을 만든다(규칙). XCOM 2 — Solomon: "We don't want the players missing multiple 85 percent shots, because then the game starts to feel punitive." [원문]. Pillars — Sawyer: graze는 "a little more annoying than helpful" [원문]. Into the Breach는 없앴다: "every death felt like your own fault" [원문]. 세 사례 모두 **빗나감**에 관한 것이다 — 유리한 쪽만의 굴림(우리 치명타)을 직접 받치는 사례는 없다.
- 출력 무작위에 대한 반론: Burgun "Output randomness is noise injected between the player's decision and the outcome." [원문, 의견 글].
- "기대 = 도파민 = 쾌감"은 근거로 쓰지 않는다(Fiorillo 외 2003은 불확실성 신호에 관한 원숭이 연구 [요약]; Berridge·Robinson 1998은 반대 방향).

## 1. 층별 뼈대 (근본적인 것부터)

### L0 런의 끝과 길이
- **명작**: 조사한 게임은 모두 끝이 있는 런이다. 도전 사다리는 첫 클리어 뒤에 열린다(Hades Pact, Slay the Spire Ascension, Darkest Dungeon Stygian [2차]). Hades는 이야기의 매듭을 여러 지점에 둔다 — Kasavin: "a strong sense of resolution at various points, so that players could happily walk away" [원문]. Into the Breach는 런 길이를 고르게 한다 — Davis: "you'd choose how many islands you'd do… customizing a play experience, like a board game" [원문].
- **지금**: 확정 — 던전은 계속 이어지고 런은 전멸로만 끝난다(23, 156). "런 성공"은 정의가 없다(F8). **미결** — 승리 지점(테마 최종 보스 격파 = 승리, 이후 계속 여부 선택; 권고: 채택)(238).
- **판정**: **뼈대의 가장 근본적인 미결.** 승리가 없으면 "첫 클리어 뒤"가 없고, 난이도 사다리·이야기의 매듭·런 길이 선택이 모두 설 자리가 없다. 조사한 명작과 가장 크게 다른 지점이다. → §3 Q0.
- **다음**: 전멸까지 이어지는 런의 선례가 있는가, 고정된 승리가 무엇을 주는가(R4-1).

### L1 턴의 행동 경제와 위치
- **명작(사례)**: Into the Breach는 8x8 격자에 메크 3기이고 작게 만든 이유를 말한다 — Davis: "since they're only 8x8 tiles in size, it would be much cheaper to hand-design 100 maps" [원문]. 난이도를 수로 올리면 "needlessly complex and hard to parse" (Ma) [원문]. XCOM: Chimera Squad는 턴을 섞었다 — Nauta: "The biggest change in combat is the introduction of interleaved turns." [원문].
- **지금**: 확정 — 이동 AP 1 / 스킬 AP 2 분리, 속도 기반 순서 + 순서 예측 표시(53~62). 격자·이동 범위·적 수는 한 번도 명작과 견준 적이 없다.
- **판정**: **조사 안 된 층.** 피해가 확정적일 때 위치를 결정으로 만드는 것이 무엇인지(밀기, 길막, 지형, 유닛당 칸 수)가 비어 있다. → R4-2 + 현재 수치 표(R4-0).

### L2 파티와 스킬의 정체성
- **명작**: 스킬은 매 턴 기본 행동과 경쟁해서 이겨야 한다 — Chimera Squad, Nauta: 요원마다 "something they could do each and every turn besides taking a standard shot action", 그것은 "had to be awesome because it is always competing against just shooting the enemy!"; "no agents share skills with each other" [원문]. 목록을 늘리지 말고 스킬마다 다른 값을 — Banner Saga: 능력의 "ninety percent of which become useless"를 피했다 [원문]. 스킬의 일은 한눈에 — Into the Breach, Ma: "we would sacrifice cool ideas for the sake of clarity every time" [원문]. 지배 전술은 깎는다 — Larian: "Every fight needed to have multiple solutions"; "a dominant tactic. So we had to nerf it a lot" [원문]; Darkest Dungeon 2 패치 노트는 한 스킬을 "a round 1 delete key"라며 고쳤다 [원문]. 조합은 관찰로 가르친다 — Larian: "the AI started teaching players new mechanics" [원문].
- **지금**: 4인, 캐릭터당 스킬 4개(풀 8개에서 런 전에 선택), 클래스 사이에 일부 스킬 공유(mage·healer의 heal·bolt·buff).
- **판정**: 구조는 일치(런 전 선택 = Monster Train 클랜 선택과 같은 자리). **확인할 것 둘**: (1) 스킬 4개가 서로를 지배하지 않는가 — 런 로그 `room_end.skills_used`로 잰다. (2) 클래스 사이 스킬 공유는 Chimera Squad의 "공유 없음"과 반대다(한 게임의 사례). 힐러의 빈 턴 문제는 자료가 없다 [미확인].

### L3 전투의 정보와 주사위
- **명작(규칙)**: 적의 다음 행동을 미리 보여준다 — Into the Breach [원문]; Slay the Spire는 처음엔 없었다 — Yano: "Enemies were very random and we had no idea what they were going to do" [원문, Rock Paper Shotgun 2018-02-19]. 숫자를 드러내고 결과는 한 박자로 — Darkest Dungeon [원문, `dd-despair/affl.txt:238, :242`].
- **지금**: 적 의도 표시, 피해 미리보기, 빗나감 없음, 아군 치명타 15%(미리보기에 치명타 피해·확률).
- **판정**: 일치. "의도 공개 + 유리한 쪽만의 굴림" 조합은 조사한 게임에 없다(근거 없음이지 반증이 아니다) → WP-39 가설로 판정.

### L4 조우와 목표
- **명작(사례)**: 보너스 목표를 주 위협과 부딪히게 둔다 — Into the Breach: "destroy the dam … or … save a city … but not both"; Ma: "what you feel is the highest priority in a given situation changes because of a mission" [원문]. 압박에 대한 견해는 갈린다 — Solomon: "if there is no pressure … play every mission almost exactly the same way"이지만 "We should've included an option to turn off the timers" [원문]; Invisible, Inc.의 Lantz: 경보는 "feels unfair to many players" [원문].
- **실패·수정 사례**: XCOM 2 타이머 반발, Invisible, Inc. 경보(가시성으로 보완, "one huge problem still unaddressed").
- **지금**: 확정 — 일반 전투의 승리 조건은 적 제거, 보너스 목표는 승리 조건을 바꾸지 않는다(63, 246). 보너스 목표 2종(`Unbroken`, `Swift`).
- **판정**: 확정 유지. **버티기가 실제로 일어나는지는 측정된 적이 없다** — 런 로그의 전투당 라운드 수를 먼저 본다. 목표를 "부딪히는" 형태로 넓히는 것은 확정 안에서 가능한 확장 후보(보상이 서로 다른 목표 둘 중 하나만 달성 가능하게).

### L5 런의 구조 (경로·엘리트·이벤트)
- **명작**: 경로 선택은 독립된 결정 층 — Giovannetti: "layered decision-making… navigating the map adds an extra element of risk/reward and planning" [원문]. 무작위는 내용 생성에(입력 무작위) — FTL: "the randomly generated galaxy, the way unexpected loot influences your strategy" [원문]. 엘리트는 고유한 적이 주류다 — Slay the Spire는 막마다 이름 있는 엘리트, 보상은 유물 [2차]. 수식어는 하는 일을 말해야 한다 — GGG: "Mods say what they do rather than having a thematic name you must learn and remember" [보관본, 3.20 선언문].
- **실패·수정 사례**: PoE Archnemesis(겹치고 읽기 어려운 수식어 → 교체). FTL의 숨은 이벤트 결과: "I just can't get behind a game where the outcome of two hours of play all hinge on a coin-flip like this" (Shamus Young) [원문].
- **지금**: 던전당 갈림길 1개, 경로 띠, 엘리트 = 같은 조우 + 수식어 1개(효과를 고르기 전에 표시), 이벤트는 대가 확정·보상 범위 표시(WP-41).
- **판정**: 일치(최소 형태). 엘리트 전용 조우는 확장 후보(콘텐츠). **정정**: "Hades의 문이 다음 보상을 보여준다"는 확인하지 못했다 [미확인] — `expansion-plan.md` §5.1의 그 근거는 사실로 인용하지 않는다.

### L6 소모와 캠프
- **명작**: 원문으로 확인된 것은 둘이다. Into the Breach는 유닛 회복을 **공짜**로 뒀다 — 수리비는 "reduced players' inclinations to put them in harm's way… so we felt it was more important to… fully heal them after every battle" [원문]. Darkest Dungeon은 스트레스 회복을 슬롯이 한정된 마을에 묶었다 — "limited slots in town for each activity… a bit of a board game" [원문]. Slay the Spire의 휴식처는 "Rest: Heal for 30% of your max HP" 또는 Smith 택일이다 [보관본 위키] — 설계 이유는 [미확인]. FTL·Hades·Monster Train·Dead Cells의 회복 규칙은 [2차]다.
- **공용 자원(게이지)의 사례**: 받아들여진 것은 손실이 "그 턴에 보이고 막을 수 있는" 형태다 — Into the Breach: "players always know where the enemy are attacking next so each turn they have a chance to disrupt them" [원문]. 시간으로 닳는 것은 불만을 샀다(위 L4의 Invisible, Inc., XCOM 2).
- **지금**: 확정 — 캠프의 긴장은 "강화 물질을 스탯 강화 / 치료 / 초기화권 중 어디에 쓸지"(110, 113); 부상·스트레스 누적 폐기(106); 공용 실패 게이지(등불) 폐기 — "벌점이 중복되고, 실패 이유가 '게이지가 닳아서'로 희석된다"(105, 112, 172). **구현**: 캠프 자동 회복(공짜) + 유료 치료가 함께 있다.
- **판정**: (1) **공짜 자동 회복은 확정 113에서 벗어나 있다** — 확정대로면 치료는 물질의 용도다. 다만 Into the Breach의 사례는 "유닛 회복을 싸게"를 지지한다. 둘이 엇갈리므로 사용자 판단(§3 Q1). (2) 공용 게이지 폐기는 조사와 같은 방향이다 — 다시 올릴 근거는 없다. (3) "긴장을 싣는 자원이 없다"는 2라운드의 판정은 **가설**로 낮춘다: 이번 플레이테스트의 갈림길 선택(WP-41 폐기 조건)이 판정한다.
- **다음**: 회복이 공짜인 분대 전술 게임은 긴장을 어디에 두는가(R4-3).

### L7 빌드
- **명작**: 스킬 + 보조 + 태그(PoE [요약]). 희소한 것이 탐나는 것 — Wilson: "highly sought after and coveted" [원문]. 발견이 보상 — Wilson [원문]; 무한 콤보는 막는다 — Mega Crit: "makes the actual playing of the game trivial" [원문]; 터지는 런은 허용한다 — Kasavin: "memorable, breakthrough runs where you sail to victory" [원문]. 죽은 선택지는 선택률로 — Mega Crit: "basically not a card in our game at that point" [원문]. 기회비용 없는 힘은 실패 — GGG: "this item didn't have much of an opportunity cost, since you could slap it on anything" [보관본]. 획득 실패에는 바닥 — Last Epoch: "the last attempted craft always still succeeds" [원문]. 강한 조각은 전제로 잠근다 — Hades 듀오: "can only be obtained when you have the prerequisite boons" [보관본 위키]. 런의 성격을 바꾸는 조각 — Giovannetti: "a relic that totally changes the nature of the run" [원문]; Hades의 망치는 "usually won't be able to acquire more than two per run" [원문].
- **규칙을 바꾸는 조각의 설계 규칙(GGG 원문, `r3/keystones`)**: "Keystones should follow the premise of \"give something, take something\"" · "keep as few stats on the keystone as necessary and make it more or less instantly understandable" · "should significantly impact the way your character is played or built" · "keystones cannot be extremely widely used due to the natural costs associated with them" · 수학으로 바로 풀리는 대가는 "not very exciting" · 특정 전투 전에 리스펙을 부추기는 대가는 실패("it rewarded respecing to get CI before any pure elemental fight") · 강제하지 않는다("then you're forced into using Blood Magic, which certainly violates what I wanted keystones to do").
- **대가의 종류(실제 예, 규칙 문구는 DB·위키)**: 자원 종류를 바꿈(Blood Magic) · 방어 층을 포기(Chaos Inoculation) · 행동 선택지를 뺌(Resolute Technique: 치명타 없음) · 정보를 뺌(Runic Dome: 적 의도를 못 봄) · 경제·휴식을 뺌(Ectoplasm, Coffee Dripper) · 턴당 상한(Velvet Choker).
- **지금**: 룬(태그·소켓·희귀도 3단계), 노드 트리(수치), 물질 스탯 강화. 확정 — "룬 = 스킬의 동작/상태 변경, 런 성장(노드·물질) = 수치와 전역 파라미터"(91). 미결 — 파티 유물(보스 보상 3택 1, 런 스코프)(237, 권고: 채택).
- **판정**: 일치. 빈 곳은 **대가가 붙은, 규칙을 바꾸는 조각**이다. 확정 91에 따라 그 자리는 노드가 아니라 **룬 또는 파티 유물**이다. "정보를 빼는 대가"(적 의도 숨김)는 기준 2(손실은 읽히게)와 정면으로 부딪히므로 쓰지 않는다. 태그 조건 없는 룬(Brutality 등)의 기회비용은 선택률로 본다.

### L8 런 사이 (메타와 장기 목표)
- **명작**: 죽음과 재시작의 고통을 줄인다 — Kasavin: "to take the pain out of dying and having to restart" [원문]. 파워 누적은 논쟁 지점(Hades Mirror [원문] 대 플레이어 불만 [2차]). 해금을 무작위에 묶지 않는다 — FTL: "some of the ship unlocks were too difficult because of the randomness involved" [원문]. 사다리는 본편 뒤에, 너무 늦지 않게 — Cooke: 최고 난이도를 요구한 도전은 "a lot of players never got to experience them" [원문]. Hades의 Pact는 Ascension에서 왔다 — Kasavin: "that game's brilliantly structured endgame via its Ascension system, which inspired our Pact of Punishment system" [원문].
- **지금**: 확정 — 메타는 옵션을 팔고 파워를 팔지 않는다; 해금은 인장 구매. 선택제 사다리 없음(회차 스케일만).
- **판정**: 일치. 사다리는 L0(승리 지점)이 정해진 뒤의 확장이다. 고르는 형태(Hades)와 고정 순서(Slay the Spire)의 우열을 말한 개발자 자료는 없다 [미확인]. 사다리 단계 중 규칙 변화의 비율(Ascension 약 6/20)은 2차 집계라 규칙으로 쓰지 않는다. Hades식 "Heat의 보상이 영구 강화 재료"는 확정 규칙과 맞지 않는다.

### L9 실패의 가독성과 재시작
- **명작(규칙)**: 기준은 결과가 아니라 원인이다 — Into the Breach: "it's very clear how or why you failed so that you can feel like you can improve" [원문]; Dead Cells, Bénard: "difficult, but not unfair", "You need to know why it was your fault." [원문]. 죽음 직후를 설계한다 — Kasavin: "the moment of death isn't about rage-quitting" [원문]; Spelunky의 베이스캠프: "a way to feel a sense of progression even if they died" [원문, 강연 보도].
- **실패 사례**: FTL — "its deaths don't always offer a useful lesson" [원문, 비평].
- **되돌리기**: 견해가 갈린다 — Tom Francis는 무료 되돌리기를 공정성 수단으로 본다 [원문]; Fire Emblem의 되돌리기는 긴장을 없앤다는 비평 [2차 의견]. 측정 자료 없음.
- **지금**: 런 디브리프, 런 로그, 확정 — 재시작 2분.
- **판정**: 일치. 다음 플레이테스트의 관찰 항목: 전멸 뒤에 플레이어가 원인을 말할 수 있었는가. "2분"에 견줄 외부 수치는 없다 [미확인].

## 2. 뼈대의 미결과 확장 후보 (근본적인 순)
| 순 | 항목 | 층 | 성격 | 먼저 필요한 것 |
|---|---|---|---|---|
| 1 | 런의 끝(승리 지점) | L0 | 확정을 바꾸는 미결 | 사용자 판단 Q0 (+R4-1) |
| 2 | 행동 경제·위치의 깊이 | L1 | 조사 안 됨 | R4-0, R4-2 |
| 3 | 캠프 회복(공짜 대 물질) | L6 | 구현이 확정에서 벗어남 | 플레이테스트 + Q1 (+R4-3) |
| 4 | 스킬 지배·전투 라운드 수·선택률 측정 | L2·L4·L7 | 측정(플레이어에게 안 보임) | 런 로그 집계 도구 |
| 5 | 대가가 붙은 규칙 변경 조각 | L7 | 확장 | 파티 유물 미결 판단(Q2) |
| 6 | 부딪히는 보너스 목표 | L4 | 확장(확정 안) | WP-39 플레이테스트 결과 |
| 7 | 선택제 난이도 사다리 | L8 | 확장 | 1번 |
| 8 | 엘리트 전용 조우, 콘텐츠 양 | L5 | 콘텐츠 | 뼈대 뒤 |
| 9 | 밸런스 | 전부 | 수치 | 맨 마지막(시뮬레이션·옵티마이저) |

지금은 WP-39~41이 병합되어 다음 플레이테스트까지 플레이어에게 보이는 출시를 멈춘다. 4번(집계 도구)은 플레이어 표시가 없어 제한에 걸리지 않는다.

## 3. 사용자 판단이 필요한 것
| # | 질문 | 선택지 | 권고와 근거 |
|---|---|---|---|
| Q0 | 런에 승리 지점을 둘 것인가 (미결 238) | 둔다(테마 최종 보스 격파 = 승리, 이후 계속 여부 선택) / 지금대로(전멸까지) | **둔다.** 조사한 명작은 모두 끝이 있는 런이고, 사다리·매듭·런 길이 선택이 그 위에 선다(L0). "계속 여부 선택"은 Into the Breach의 런 길이 선택과 같은 자리다. 확정 "종료는 전멸뿐"을 바꾼다 |
| Q1 | 캠프 회복 | 확정대로(자동 회복 없음, 치료는 물질) / 지금대로(공짜 자동 회복 + 유료 추가 치료) | **플레이테스트 뒤 판단.** 확정 113과 Into the Breach의 사례가 엇갈린다. 판정 신호: 갈림길에서 엘리트를 이유 없이 매번 고르는가(위험이 싸다 → 확정대로), 몸을 사리는가(→ 지금대로) |
| Q2 | 파티 유물(미결 237): 보스 보상으로 런의 성격을 바꾸는 조각 3택 1 | 채택 / 보류 | **채택하되 Q0 뒤에.** 규칙을 바꾸는 조각의 자리로 확정 91과 맞고(L7), 근거는 Slay the Spire 유물·Hades 망치. GGG의 설계 규칙(주고 빼앗는다, 한눈에 이해)을 따른다. 확정 "드롭 대상: 룬, 강화 물질"을 바꾼다 |

앞 라운드의 질문(소모 압박 A/B/C, 사다리 형태, 사다리 보상)은 이 표로 대체한다: 공용 자원(B)은 확정으로 기각된 방향이고, 사다리는 Q0 뒤의 일이다.

## 4. 다음 라운드 (R4)
- **R4-0 현재 수치 표**(로컬): 격자 크기, 이동 범위, 전투당 아군·적 수, 유닛당 칸 수, 스킬 사거리, 던전당 방 수 — 명작의 수치와 나란히 놓는다.
- **R4-1 런의 끝**: 전멸까지 이어지는 런과 고정된 승리의 선례·평가.
- **R4-2 위치의 깊이**: 확정 피해 아래에서 위치 결정을 만드는 것(밀기, 길막, 지형, 밀도, AP 모델).
- **R4-3 공짜 회복 분대 게임의 긴장**: Into the Breach 외의 사례.
- **R4-4 실패·수정 사례**: 출시 뒤 고친 전술·덱빌딩 로그라이크(무엇이 왜 고쳐졌는가) — 생존 편향 보정.
- **R4-5 성장 분배와 관리 부담**: 처치 EXP(확정 83, 88)와 "킬 몰아주기", 캠프 관리 시간.
- 아직 못 푼 것: Mega Crit의 휴식처·보스 유물·Ascension 설계 이유, Hades 문 보상, Darkest Dungeon 횃불·후퇴·"7개 중 4개". GDC Vault 본문 접근 불가. 웹 검색 한도(세션당 약 200회)에 걸려 R3 일부가 미완.

## 5. 라운드 기록
- **R1**: 런 구조 / Darkest Dungeon / 빌드 / 전투 정보 / 무작위 이론 / 메타·페이싱.
- **R2**: 소모 자원 / 휴식처·상점 / 도전 사다리 / 빌드 완성 구조 / 소규모 분대 조우 / 엘리트·이벤트.
- **비평 1차(egm-design)**: 런의 끝이 빠짐; 소모 압박 선택지와 확정 결정의 관계가 틀림; "7개 중 6개" 과장; 한 게임의 선택을 규칙처럼 씀; 빠진 층 5개. → 이 개정에 반영.
- **R3**: 인용 원문 검증(6건 확인, 1건 정정, 1건 미확인) / 공용 자원 / 키스톤·대가 / 파티 역할 / 실패의 가독성. 한계: 검색 한도, wiki.gg·fandom 차단.
