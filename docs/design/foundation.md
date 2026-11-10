# 설계 뼈대와 근거 (improve-loop 기록)

상태: 2라운드 반영(2026-11-11). 이 문서는 Ember Dungeon의 뼈대를 층별로 적고, 층마다 "명작이 실제로 한 것 → 지금 우리 게임 → 판정"을 근거와 함께 남긴다. 순서는 **뼈대 → 체계적 확장 → 밸런스(맨 마지막)**. 수치는 여기서 다루지 않는다(시뮬레이션·옵티마이저의 일). 구현 설계는 각 WP 스펙이 맡는다. 확정 결정(`decisions.md`)을 바꾸는 항목은 "사용자 판단" 표에만 올린다.

근거 표기: **[원문]** 원문과 대조된 인용 · **[요약]** 요약 도구를 거친 인용(결정의 근거로 쓰기 전 원문 대조 필요) · **[2차]** 위키·기사 · **[미확인]** 1차 자료를 찾지 못함. 조사 원본은 `docs/digests/r1/`, `docs/digests/r2/`(로컬, 추적 안 함).

## 0. 판단 기준 (기존 확정)
기대의 극대화 + 적절한 변수성. 보상은 불확실하게, 손실은 읽히게. 입력은 공개, 결과는 연출. 무작위는 "언제·어디서"에. (`expansion-plan.md` §1)

1라운드가 이 기준에 더한 것:
- **플레이어가 이미 정한 행동의 결과를 깎는 무작위가 불만을 만든다.** XCOM 2는 표시 확률을 몰래 보정한다 — Solomon: "We don't want the players missing multiple 85 percent shots, because then the game starts to feel punitive." [원문, gamedeveloper.com]. Pillars는 graze를 없앴다 — Sawyer: "people tend to find it a little more annoying than helpful" [원문, talkingames.com]. Into the Breach는 아예 없앴다 — "We wanted to make something where every death felt like your own fault. This lead us to use of telegraphed enemy attacks as a core mechanic." [원문, gamedeveloper.com Road to the IGF].
- **출력 무작위는 깊이를 더하지 않는다는 반론이 있다.** Burgun: "Output randomness is noise injected between the player's decision and the outcome." [원문, `docs/digests/burgun-randomness/`]. 의견 글이며, 조율된 출력 무작위가 도움이 된다는 반대 견해도 있다 [2차].
- **"기대 = 도파민 = 쾌감"은 근거로 쓰지 않는다.** Fiorillo·Tobler·Schultz 2003은 불확실성 신호가 p=0.5에서 최대이고 보상 시점까지 오른다는 원숭이 전기생리 결과다 [요약]. Berridge·Robinson 1998은 도파민이 쾌감 자체를 매개하지 않는다고 본다. "Sapolsky의 anticipation" 인용은 찾지 못했다 [미확인].

## 1. 층별 뼈대

### L1 런의 구조 (던전 → 방 → 캠프)
| 명작의 규칙 | 근거 | 지금 우리 게임 | 판정 |
|---|---|---|---|
| 경로 선택은 전투 밖의 독립된 결정 층이다 | Giovannetti: "it is valuable to have layered decision-making… navigating the map adds an extra element of risk/reward and planning" [원문, gamedeveloper.com] | 던전당 갈림길 1개(WP-41), 경로 띠 | 일치(최소 형태). 넓히는 것은 플레이테스트 후 |
| 무작위는 런의 "내용 생성"에 둔다(입력 무작위) | Slay the Spire 절차 생성 층, Hades 방 순서·적 무작위 [2차 Wikipedia]; FTL "the randomly generated galaxy, the way unexpected loot influences your strategy" [원문, rpgcodex] | 조우·이벤트·갈림길·제안이 시드로 결정 | 일치 |
| 실패는 플레이어의 결정으로 되짚을 수 있어야 한다 | FTL: "each failure can be traced back to a decision the player has made" [원문, rpgcodex]; Into the Breach(위) | 런 디브리프·런 로그 | 일치. 디브리프가 "어느 결정"인지 짚는지는 플레이테스트에서 확인 |
| 런을 관통하는 **소모 자원**이 위험 선택에 값을 매긴다 | Slay the Spire(HP가 전투 사이에 이어짐), Darkest Dungeon(스트레스가 원정과 마을까지 이어짐 — Sigman: "the meta-strategy of persistent stress" [원문, dd-despair]) | HP가 싸다: 방마다 회복, 쓰러져도 25% 복귀, 캠프 자동 회복. 다른 누적 압박 없음 | **빈 곳(가장 큼).** R2: 7개 중 6개가 공짜로 차지 않는 런 단위 자원을 둔다. 유닛 HP가 공짜인 Into the Breach는 긴장을 공유 자원(전력망)으로 옮겼다 → §2.1 |
| 휴식처는 "회복 대 성장"의 맞교환 | Slay the Spire 캠프파이어(휴식 또는 강화) [미확인: 설계 이유 1차 자료 없음] | 캠프: 자동 회복 + 물질 한 화폐로 강화·제안·회복 | 화폐 공유는 명작과 같다(Slay the Spire 골드, FTL 스크랩, Into the Breach 평판 [2차]). **다른 점은 공짜 자동 회복** → §2.1 |

### L2 전투 (정보와 주사위의 위치)
| 명작의 규칙 | 근거 | 지금 우리 게임 | 판정 |
|---|---|---|---|
| 적의 다음 행동을 미리 보여준다 | Into the Breach(위); Slay the Spire 의도 표시 — 원래 숨겼으나 "did not mesh well with the roguelike nature of permadeath" [2차 Wikipedia 요약] | 적 의도 표시, 피해 미리보기 | 일치 |
| 플레이어 쪽 주사위는 없거나, 있으면 불리한 쪽을 다듬는다 | Into the Breach(없음), XCOM 2(연속 빗나감 보정), Pillars(graze 삭제) | 빗나감 없음. 아군 치명타 15%(유리한 쪽만), 미리보기에 치명타 피해·확률 표시 | 일치. "의도 공개 + 플레이어 쪽 유리한 굴림"은 조사한 6개 게임에 같은 조합이 없다(표본이 작음) → 플레이테스트 가설(WP-39)로 판정 |
| 숫자를 드러내고, 결과는 한 박자로 연출 | Darkest Dungeon 스트레스 [원문, dd-despair affl.txt:238, :242] | 치명타 연출, 확률 표시 | 일치 |
| 시간 압박이 없으면 한 칸씩 전진하는 플레이가 나온다 | Invisible, Inc.: X-COM의 비폭탄 임무는 "has no time pressure and allows players to advance, inch by inch"; 목표는 "interesting tradeoffs with every move, and keeping busywork to a minimum" [원문, gamedeveloper.com Lantz] | 전멸 조건. 압박은 보너스 목표 `Swift`(+1 물질)뿐 | **빈 곳** → §2.3 |
| 보너스 목표는 주 위협과 부딪히게 둔다 | Into the Breach: "destroy the dam … or … save a city … but not both"; Ma: "what you feel is the highest priority in a given situation changes because of a mission" [원문, gamedeveloper.com] | `Unbroken`, `Swift`: 잘하면 받는 덤 | **빈 곳** → §2.3 |
| 난이도를 적의 수로 올리지 않는다 | Ma: "flood the board with enemies and danger zones, which becomes needlessly complex and hard to parse" [원문] | 엘리트는 수식어 1개, 적 추가 없음(WP-41) | 일치 |

### L3 빌드 (런 안의 성장)
| 명작의 규칙 | 근거 | 지금 우리 게임 | 판정 |
|---|---|---|---|
| 스킬 + 보조(서포트) + 태그 | PoE: 서포트는 "augment the skill gems they are linked to" [요약, poedb] | 스킬 + 룬(태그 엄격, 소켓) | 일치 |
| 규칙을 바꾸는 노드(키스톤) | Wilson: "a lot of the keystone ones dramatically change how you play your character… they provide new modes for existing skills" [원문, poe-maroon] | 노드 트리에 수치 노드만 있음 | **빈 곳** → §2.4 |
| 희소한 것이 탐나는 것이 된다 | Wilson: "rare to see someone using that skill, but if they do have it then it's something that's highly sought after and coveted" [원문, poe-maroon] | 룬 희귀도 3단계(WP-40) | 일치 |
| 발견이 보상이다 / 무한 콤보는 막는다 | Wilson: "it can be very rewarding… to discover a certain build that immediately makes you very effective" [원문]; Mega Crit: "Going infinite… makes the actual playing of the game trivial" [원문] | 위력 증가 상한(+100%) | 일치. R2: Kasavin은 "memorable, breakthrough runs where you sail to victory"를 일부러 허용한다 [원문, gamesradar] — 상한이 "터지는 런"을 막는지는 플레이테스트로 본다 |
| 죽은 선택지는 선택률로 찾는다 | Mega Crit: 거의 안 뽑히면 "basically not a card in our game at that point" [원문] | 런 로그에 제안·선택·등급 기록(WP-40) | 도구는 있음. 집계 스크립트가 없다 → 도구 WP 후보 |
| 기회비용 없는 강화는 실패 | GGG Hrishi: "this item didn't have much of an opportunity cost, since you could slap it on anything" [요약, pathofexile.com 포럼] | 태그 조건 없는 룬(Brutality, Quicken, Far Reach, Counter Guard). 비용은 소켓 수뿐 | **검토 필요.** 소켓 1~3개가 기회비용으로 충분한지 → 플레이테스트·선택률로 판정 |
| 제작·획득의 실패는 바닥을 둔다 | Last Epoch: "the last attempted craft always still succeeds" [요약, 포럼 개발자 글] | pity, 건너뛰기 환급, 중복 환급 | 일치 |

### L4 런 사이 (메타)
| 명작의 규칙 | 근거 | 지금 우리 게임 | 판정 |
|---|---|---|---|
| 죽음과 재시작의 고통을 줄인다 | Kasavin: "It was an explicit goal… to take the pain out of dying and having to restart" [원문, gamedeveloper.com]; "every run counts" [원문, tomsguide] | 재시작 2분 규칙(확정), 체크포인트 | 일치 |
| 파워 누적은 논쟁 지점 | Hades Mirror of Night: "to make you meaningfully more powerful from one playthrough to a next" [원문]; 플레이어 불만 "I only succeeded because of the meta progression" [2차] | "메타는 옵션을 팔고 파워를 팔지 않는다"(확정) | 일치(Slay the Spire·Into the Breach·FTL 계열) |
| 난이도 상승은 승리 후 선택제 | Slay the Spire Ascension, Hades Pact, Monster Train Covenant, Dead Cells Boss Cells [2차] | 회차(cycle) 스케일만 있음. 선택제 사다리 없음 | **빈 곳** → §2.2 |
| 해금을 무작위에 묶지 않는다 | FTL: "some of the ship unlocks were too difficult because of the randomness involved" [원문, shacknews] | 해금은 인장 구매 | 일치 |
| 옵션만 파는 게임의 장기 목표: 콘텐츠 양, 도전 사다리, 이야기의 매듭 | Hades: "a strong sense of resolution at various points, so that players could happily walk away" [원문]; Monster Train 도전 [2차] | 보스 목표, 해금 | 사다리·매듭이 없다 → §2.2 |

## 2. 뼈대의 빈 곳과 선택지 (우선순위 순)

### 2.1 런을 관통하는 소모 압박 + 캠프 (L1)
R2 근거 (`r2-attrition.md`, `r2-rest-stops.md`):
- 7개 중 6개가 런 단위 HP성 자원 하나를 둔다(Slay the Spire HP, Monster Train 파이어, Into the Breach 전력망, FTL 선체, Hades HP, Dead Cells 플라스크) [Into the Breach·Darkest Dungeon·Hades는 원문, 나머지 2차].
- 회복은 자동 재생이 아니라 들르는 곳·행동이다(5/7). 회복은 성장과 같은 정거장·같은 화폐를 두고 경쟁한다(4/7).
- 반대 방향의 경고도 있다. Into the Breach는 수리 비용을 검토했다 버렸다: "It makes you want to protect your mechs more… But it also reduced players' inclinations to put them in harm's way… so we felt it was more important to… fully heal them after every battle" [원문, gamedeveloper.com]. 대신 긴장을 (1) 런 공유 자원(전력망 7칸), (2) 조종사 영구 사망, (3) 전투마다 부딪히는 목표로 옮겼다.
- Darkest Dungeon은 HP가 아닌 자원(스트레스)을 슬롯이 한정된 마을 회복에 묶었다: "limited slots in town for each activity… a bit of a board game" [원문].

묶여 있는 확정 결정: 캐릭터 부상·사망 없음, 쓰러져도 패널티 무누적, 부상·스트레스 누적 폐기(`decisions.md` 9, 52, 106행).

| 선택지 | 내용 | 근거가 되는 게임 | 확정 결정과의 관계 | 위험 |
|---|---|---|---|---|
| A. HP를 런 자원으로 | 캠프 자동 회복을 없애고, 캠프에서 "휴식 또는 단련" 중 하나를 고르게 한다. 방 사이 회복은 유지하거나 줄인다 | Slay the Spire(휴식 대 강화), FTL(수리가 성장과 같은 화폐) | 충돌 없음(HP는 지금도 방 사이에 이어진다) | Into the Breach의 경고: HP가 비싸면 유닛을 위험에 내보내지 않는다. 전술 게임에서 HP는 도구다 |
| B. 파티 공유 자원 신설 | 런 단위 공유 수치(예: 불씨)를 두고, 전투의 목표 실패나 특정 사건에만 깎이고 목표 달성으로만 오른다. 유닛 HP는 지금처럼 싸게 둔다 | Into the Breach(전력망), Darkest Dungeon 2(불꽃 [2차]) | "쓰러짐 무누적"과 닿는다: 쓰러짐에 연동하면 위반, 목표에만 연동하면 무관 | 새 시스템 하나. 0이 됐을 때의 규칙이 필요하다 |
| C. 지금대로 | 긴장은 전멸(런 종료)과 보너스 목표에서만 나온다 | 없음(조사한 게임 중 이 형태는 없다) | 충돌 없음 | 엘리트·대가형 이벤트의 "위험"이 싸다 |

권고: **이번 플레이테스트로 C를 먼저 판정한다**(WP-41의 폐기 조건 "모든 갈림길에서 같은 가지"가 바로 이 신호다). 엘리트가 싸게 느껴지면 A부터 — 새 시스템이 없고 확정 결정과 충돌이 없다. A에서 플레이어가 몸을 사리면(Into the Breach의 경고) B로 간다. → 사용자 판단 Q1.

### 2.2 선택제 난이도 사다리 (L4)
R2 근거 (`r2-challenge-ladders.md`):
- Hades의 Pact는 Slay the Spire의 Ascension에서 왔다 — Kasavin: "we were particularly drawn to that game's brilliantly structured endgame via its Ascension system, which inspired our Pact of Punishment system"; "structured challenges to keep you invested long after your first successful clear" [원문, tomsguide].
- 모든 사례에서 사다리는 본편을 한 번 끝낸 뒤에 열린다 [2차]. 너무 늦게 여는 것은 실패 — Cooke: 최고 난이도 클리어를 요구한 도전은 "a lot of players never got to experience them" [원문, gamepressure].
- 단계의 다수는 수치지만 설계의 요점은 규칙을 바꾸는 단계다(Ascension 20단계 중 약 6개, Pact 15개 조건 중 4개가 선택지를 뺀다) [2차 집계].
- 보상: Hades는 Heat에 영구 강화 재료를 준다(위험의 대가로 파워) — 우리 확정 규칙과 맞지 않는다. Slay the Spire·Darkest Dungeon은 진행도·해금을 준다 — 맞는다.
- 런 길이를 고르게 하는 것도 선택제다 — Davis: "you'd choose how many islands you'd do… customizing a play experience, like a board game" [원문, gamedeveloper.com].

뼈대 규칙(확장 때 지킬 것): 첫 클리어 뒤에 연다 · 단계마다 규칙 하나(수치만 올리는 단계는 절반 이하) · 보상은 해금·기록이지 파워가 아니다 · 플레이어가 고른다. 선택형(Hades)과 고정 순서(Slay the Spire)의 우열은 개발자 자료가 없다 [미확인] → 사용자 판단 Q2.

### 2.3 전투의 시간 압박과 부딪히는 목표 (L2)
R2 근거 (`r2-encounters.md`): §1 L2 표의 Invisible, Inc.·Into the Breach 인용. Into the Breach의 시계는 턴 제한이 아니라 자원이다 — 적의 공격이 매 턴 보이고 "each turn they have a chance to disrupt them" [원문], 못 막으면 공유 자원이 깎인다. 보너스 목표 보상은 세 자원으로 갈린다 [원문·2차].
지금: 전멸 조건, 압박은 `Swift` +1 물질뿐. 보너스 목표는 주 위협과 부딪히지 않는다.
방향: 2.1의 B(공유 자원)를 택하면 "전투마다 지켜야 할 것"이 자연히 시계가 된다(Into the Breach와 같은 묶음). A를 택하면 목표를 "무엇을 포기할지" 형태로 바꾸는 것이 따로 필요하다. WP-39 가설(목표 때문에 수를 바꿨는가)의 플레이테스트 결과를 먼저 본다.

### 2.4 규칙을 바꾸는 빌드 조각 (L3)
R2 근거 (`r2-build-around.md`):
- 런의 성격을 바꾸는 조각이 있다 — Giovannetti: "You might get a relic that totally changes the nature of the run" [원문, pcgamer 2018]; "Allowing for combos and strong synergies is definitely a hallmark", 동시에 "not just a single dominant strategy" [원문, Road to the IGF].
- 빌드 정의 조각은 런마다 한두 개가 일찍 정해진다 — Hades의 Daedalus 망치: "usually won't be able to acquire more than two per run", 첫 방에서도 나온다 [원문, pcgamer]; Monster Train은 런 전에 클랜 두 개를 고른다 — clans "define your starting conditions and which cards are available to draft" [원문, Cooke].
- 강한 조각은 조건(전제)으로 잠근다 — Hades 듀오·전설 은혜는 전제 은혜가 필요하다 [요약, 팬 가이드]. "가진 것 쪽으로 제안을 기울인다"는 1차 근거는 없다 [미확인].
- 대가가 붙은 힘: Slay the Spire 보스 유물 [요약], Hades Chaos [요약]; Dead Cells는 과한 무기를 깎지 않고 비용을 붙였다 [원문, gamedeveloper.com Benard].

지금: 런 전에 스킬을 고른다(Monster Train의 클랜 선택과 같은 자리) — 일치. 런 중의 "성격을 바꾸는 조각"은 희귀 룬뿐이고, 노드 트리에는 없다.
방향(확장 후보): 노드 트리 키스톤(규칙을 바꾸고 대가가 있는 노드) · 전제 조건이 있는 룬(특정 룬 둘을 가졌을 때만 뜨는 것). 둘 다 "런 EXP가 노드 포인트의 공급원"(확정) 안에서 가능하다.

### 2.5 엘리트의 형태 (L1, WP-41 후속)
R2 근거 (`r2-elites-events.md`): Slay the Spire의 엘리트는 막마다 이름 있는 고유 적이고 보상은 유물이다. 수치 강화형은 "불타는 엘리트"에만 쓴다 [2차 위키]. PoE Archnemesis는 겹치고 읽기 어려운 수식어로 반발을 사 3.20에서 교체됐고, 방향은 수식어가 "say what they do"였다 [요약, 원문 대조 필요].
지금: 같은 조우 + 수식어 1개, 효과를 고르기 전에 보여준다 — Archnemesis의 실패는 피했다. 다만 "수치만 올린 일반 적"은 약한 형태다.
방향(확장 후보): 엘리트 전용 조우(고유 적 또는 고유 행동 패턴). 콘텐츠 작업이라 뼈대 뒤.

### 2.6 선택률 집계 도구 (L3, 플레이어에게 안 보임)
런 로그의 `offer`·`fork`·`event` 기록으로 선택률을 낸다. Mega Crit의 지표와 같다. 다음 플레이테스트 판정에 바로 쓰인다 → 도구 WP 후보(플레이어 표시 변경 없음이라 3개 제한에 걸리지 않는다).

## 3. 확장 순서 (제안, 플레이테스트 뒤에 확정)
1. 플레이테스트(WP-39~41) + 선택률 집계 도구(2.6).
2. 소모 압박과 캠프(2.1) — 뼈대. 전투의 시간 압박(2.3)은 2.1의 선택에 따라 같은 묶음이 된다.
3. 난이도 사다리(2.2) — 장기 목표.
4. 규칙을 바꾸는 빌드 조각(2.4).
5. 엘리트 전용 조우(2.5)와 콘텐츠 양.
6. 밸런스(시뮬레이션·옵티마이저) — 맨 마지막.

## 4. 사용자 판단이 필요한 것 (취향·확정 결정에 닿는 것)
| # | 질문 | 권고 | 근거 |
|---|---|---|---|
| Q1 | 런의 소모 압박을 어디에 둘 것인가: A(HP를 런 자원으로) / B(파티 공유 자원) / C(지금대로) | 플레이테스트로 C를 먼저 판정, 부족하면 A, A가 몸 사리기를 만들면 B | §2.1 |
| Q2 | 난이도 사다리: 플레이어가 조건을 고르는 형태(Hades) / 고정 순서(Slay the Spire) | 고르는 형태 — 기준 1(기대)에 맞고, "보상은 해금·기록"으로 확정 규칙을 지킨다 | §2.2 |
| Q3 | 사다리의 보상이 파워여도 되는가 | 안 된다(확정 규칙 유지) | §2.2 Hades 대 Slay the Spire |

## 5. 다음 라운드
- **R3-검증**: 결정이 기대는 [요약] 인용의 원문 대조(Archnemesis 선언문, Last Epoch 제작 글, Hades 듀오 전제, Slay the Spire 휴식 30%).
- **R3-조사**: (a) 공유 자원의 실제 설계 — Into the Breach 전력망, Darkest Dungeon 2 불꽃, Monster Train 파이어가 무엇에 깎이고 무엇으로 오르며 0에서 무슨 일이 생기는가. (b) 키스톤·보스 유물의 대가 설계 목록과 "함정 선택" 비판. (c) 파티 4인 전술 게임의 역할 구분(Darkest Dungeon 직업·위치, Into the Breach 분대)과 스킬 4개의 정체성. (d) 디브리프·실패 설명(Into the Breach "it's very clear how or why you failed", Slay the Spire 런 기록).
- 아직 못 푼 것: Slay the Spire 휴식처·Ascension·보스 유물의 설계 이유(Mega Crit 1차 자료), Hades 문 보상·은혜 등급, Darkest Dungeon 횃불·후퇴. GDC Vault 본문은 접근 불가.

## 6. 라운드 기록
- **R1 (2026-11-11)**: 런 구조 / Darkest Dungeon / 빌드 / 전투 정보 / 무작위 이론 / 메타·페이싱. 한계: GDC Vault 본문 대부분 접근 불가, 여러 인용이 [요약] 등급.
- **R2 (2026-11-11)**: 소모 자원 / 휴식처·상점 / 도전 사다리 / 빌드 완성 구조 / 소규모 분대 조우 / 엘리트·이벤트. 판정 변경: "HP가 싸다"는 그 자체로 결함이 아니다(Into the Breach) — 결함은 대신 긴장을 싣는 자원이 없는 것. 한계: 개발자 설계 이유는 Into the Breach·Hades·Monster Train(Cooke)·Invisible, Inc.만 원문 확보. 에이전트 둘이 `docs/digests/r2/`의 작업 파일을 서로 덮어써, `r2-rest-stops.md`의 검증 수치는 재확인 대상.
