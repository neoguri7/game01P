# 설계 뼈대와 근거 (improve-loop 기록)

상태: 1라운드 반영(2026-11-11). 이 문서는 Ember Dungeon의 뼈대를 층별로 적고, 층마다 "명작이 실제로 한 것 → 지금 우리 게임 → 판정"을 근거와 함께 남긴다. 순서는 **뼈대 → 체계적 확장 → 밸런스(맨 마지막)**. 수치는 여기서 다루지 않는다(시뮬레이션·옵티마이저의 일). 구현 설계는 각 WP 스펙이 맡는다. 확정 결정(`decisions.md`)을 바꾸는 항목은 "사용자 판단" 표에만 올린다.

근거 표기: **[원문]** 원문과 대조된 인용 · **[요약]** 요약 도구를 거친 인용(결정의 근거로 쓰기 전 원문 대조 필요) · **[2차]** 위키·기사 · **[미확인]** 1차 자료를 찾지 못함. 조사 원본은 `docs/digests/r1/`(로컬, 추적 안 함).

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
| 런을 관통하는 **소모 자원**이 위험 선택에 값을 매긴다 | Slay the Spire(HP가 전투 사이에 이어짐), Darkest Dungeon(스트레스가 원정과 마을까지 이어짐 — Sigman: "the meta-strategy of persistent stress" [원문, dd-despair]) | HP가 싸다: 방마다 회복, 쓰러져도 25% 복귀, 캠프 자동 회복. 다른 누적 압박 없음 | **빈 곳(가장 큼).** 엘리트의 "위험"이 실제 값이 되는지가 여기에 달렸다 → 2라운드 A |
| 휴식처는 "회복 대 성장"의 맞교환 | Slay the Spire 캠프파이어(휴식 또는 강화) [미확인: 설계 이유 1차 자료 없음] | 캠프: 자동 회복 + 물질 한 화폐로 강화·제안·회복 | **미정** → 2라운드 B |

### L2 전투 (정보와 주사위의 위치)
| 명작의 규칙 | 근거 | 지금 우리 게임 | 판정 |
|---|---|---|---|
| 적의 다음 행동을 미리 보여준다 | Into the Breach(위); Slay the Spire 의도 표시 — 원래 숨겼으나 "did not mesh well with the roguelike nature of permadeath" [2차 Wikipedia 요약] | 적 의도 표시, 피해 미리보기 | 일치 |
| 플레이어 쪽 주사위는 없거나, 있으면 불리한 쪽을 다듬는다 | Into the Breach(없음), XCOM 2(연속 빗나감 보정), Pillars(graze 삭제) | 빗나감 없음. 아군 치명타 15%(유리한 쪽만), 미리보기에 치명타 피해·확률 표시 | 일치. "의도 공개 + 플레이어 쪽 유리한 굴림"은 조사한 6개 게임에 같은 조합이 없다(표본이 작음) → 플레이테스트 가설(WP-39)로 판정 |
| 숫자를 드러내고, 결과는 한 박자로 연출 | Darkest Dungeon 스트레스 [원문, dd-despair affl.txt:238, :242] | 치명타 연출, 확률 표시 | 일치 |
| 전투마다 다른 문제를 내는 구조(승리 조건·턴 압박·적 유형) | 조사 안 함 | 전멸 조건 + 보너스 목표 2종 | **미정** → 2라운드 E |

### L3 빌드 (런 안의 성장)
| 명작의 규칙 | 근거 | 지금 우리 게임 | 판정 |
|---|---|---|---|
| 스킬 + 보조(서포트) + 태그 | PoE: 서포트는 "augment the skill gems they are linked to" [요약, poedb] | 스킬 + 룬(태그 엄격, 소켓) | 일치 |
| 규칙을 바꾸는 노드(키스톤) | Wilson: "a lot of the keystone ones dramatically change how you play your character… they provide new modes for existing skills" [원문, poe-maroon] | 노드 트리에 수치 노드만 있음 | **빈 곳** → 확장 후보, 2라운드 D |
| 희소한 것이 탐나는 것이 된다 | Wilson: "rare to see someone using that skill, but if they do have it then it's something that's highly sought after and coveted" [원문, poe-maroon] | 룬 희귀도 3단계(WP-40) | 일치 |
| 발견이 보상이다 / 무한 콤보는 막는다 | Wilson: "it can be very rewarding… to discover a certain build that immediately makes you very effective" [원문]; Mega Crit: "Going infinite… makes the actual playing of the game trivial" [원문] | 위력 증가 상한(+100%) | 일치. 시너지 발견 여지는 2라운드 D |
| 죽은 선택지는 선택률로 찾는다 | Mega Crit: 거의 안 뽑히면 "basically not a card in our game at that point" [원문] | 런 로그에 제안·선택·등급 기록(WP-40) | 도구는 있음. 집계 스크립트가 없다 → 도구 WP 후보 |
| 기회비용 없는 강화는 실패 | GGG Hrishi: "this item didn't have much of an opportunity cost, since you could slap it on anything" [요약, pathofexile.com 포럼] | 태그 조건 없는 룬(Brutality, Quicken, Far Reach, Counter Guard). 비용은 소켓 수뿐 | **검토 필요.** 소켓 1~3개가 기회비용으로 충분한지 → 플레이테스트·선택률로 판정 |
| 제작·획득의 실패는 바닥을 둔다 | Last Epoch: "the last attempted craft always still succeeds" [요약, 포럼 개발자 글] | pity, 건너뛰기 환급, 중복 환급 | 일치 |

### L4 런 사이 (메타)
| 명작의 규칙 | 근거 | 지금 우리 게임 | 판정 |
|---|---|---|---|
| 죽음과 재시작의 고통을 줄인다 | Kasavin: "It was an explicit goal… to take the pain out of dying and having to restart" [원문, gamedeveloper.com]; "every run counts" [원문, tomsguide] | 재시작 2분 규칙(확정), 체크포인트 | 일치 |
| 파워 누적은 논쟁 지점 | Hades Mirror of Night: "to make you meaningfully more powerful from one playthrough to a next" [원문]; 플레이어 불만 "I only succeeded because of the meta progression" [2차] | "메타는 옵션을 팔고 파워를 팔지 않는다"(확정) | 일치(Slay the Spire·Into the Breach·FTL 계열) |
| 난이도 상승은 승리 후 선택제 | Slay the Spire Ascension, Hades Pact, Monster Train Covenant, Dead Cells Boss Cells [2차] | 회차(cycle) 스케일만 있음. 선택제 사다리 없음 | **빈 곳** → 확장 후보, 2라운드 C |
| 해금을 무작위에 묶지 않는다 | FTL: "some of the ship unlocks were too difficult because of the randomness involved" [원문, shacknews] | 해금은 인장 구매 | 일치 |
| 옵션만 파는 게임의 장기 목표: 콘텐츠 양, 도전 사다리, 이야기의 매듭 | Hades: "a strong sense of resolution at various points, so that players could happily walk away" [원문]; Monster Train 도전 [2차] | 보스 목표, 해금 | 사다리·매듭이 없다 → 2라운드 C |

## 2. 뼈대의 빈 곳 (우선순위 순)
1. **런을 관통하는 소모 압박** (L1). 없으면 위험 선택이 값이 없다. 확정 결정("쓰러짐 무누적" 등)과 닿으므로 조사 후 사용자 판단.
2. **캠프의 결정 구조** (L1). 1번과 한 묶음.
3. **선택제 난이도 사다리** (L4). 옵션만 파는 메타의 장기 목표.
4. **규칙을 바꾸는 빌드 조각**(키스톤, 대가 있는 강화) (L3).
5. **전투의 문제 다양성**(승리 조건·턴 압박) (L2).
6. 선택률 집계 도구 (L3, 플레이어에게 안 보임).

## 3. 확장 순서 (제안, 플레이테스트 뒤에 확정)
뼈대의 빈 곳을 먼저(1→2), 그다음 장기 목표(3), 그다음 빌드 폭(4), 전투 폭(5). 콘텐츠 양(이벤트·적·룬 수)은 뼈대가 선 뒤. 밸런스는 맨 마지막.
지금은 WP-39~41이 병합되어 다음 플레이테스트까지 플레이어에게 보이는 출시를 멈춘다. 이 문서의 항목은 플레이테스트 메모와 함께 다음 WP 후보가 된다.

## 4. 다음 라운드 질문
A 소모 자원과 회복의 배급 · B 휴식처·상점의 결정 구조 · C 도전 사다리와 옵션 해금의 속도 · D 런 안에서 빌드가 "완성되는" 구조(빌드 정의 조각의 수·시점, 대가 있는 강화) · E 소규모 분대 전투의 조우 뼈대(승리 조건, 턴 압박, 보너스 목표) · F 엘리트·수식어·이벤트 공개 범위.
1라운드에서 못 푼 것: Slay the Spire 휴식처·Ascension의 설계 이유, Hades 문 보상·은혜 등급의 1차 자료, Darkest Dungeon 횃불·골동품·후퇴의 설계 이유(GDC 포스트모템 본문 접근 불가).

## 5. 라운드 기록
- **R1 (2026-11-11)**: 런 구조 / Darkest Dungeon / 빌드 / 전투 정보 / 무작위 이론 / 메타·페이싱. 한계: GDC Vault 본문 대부분 접근 불가, 여러 인용이 [요약] 등급.
