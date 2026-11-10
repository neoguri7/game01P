# Ember Dungeon — 디자인 필러 (DRAFT, 2026-11-10)

상태: **초안 — 사용자 승인 전.** 근거: `decisions.md`의 확정 결정 + 레퍼런스 게임(던전 → Darkest Dungeon, 빌드 → Path of Exile, UI → Pillars of Eternity) + 검증된 자료. 확정 결정과 충돌하는 레퍼런스 요소는 채택하지 않았다(아래 "채택하지 않은 것").
모든 WP는 아래 필러 중 하나를 섬긴다. 필러와 충돌하는 제안은 사용자 결정 없이는 들어오지 않는다.

## P1. 런마다 새 빌드 — 시너지는 발견하는 것
- 런은 자주 리셋되고, 그 리셋이 빌드 실험의 재료다. 드롭은 파워가 아니라 **선택지**를 연다.
- 좋은 빌드는 조합을 발견했을 때 즉시 체감된다.
- 지배 빌드는 너프만이 아니라 **대항 선택지 추가**로 푼다.
- 근거: decisions §1 "자주 리셋될 때 가장 맛있다", "드롭의 목적은 '선택지 개방'", 실패 모드 "최적 빌드 수렴"(확정) · PoE Chris Wilson: "a lot of synergy for players to discover … rewarding … to discover a certain build", "adding additional skills that provide people with outs against those dominate strategies" ([maroonersrock 3/3](https://maroonersrock.com/2012/02/interview-chris-wilson-path-of-exile-part-3-of-3), [1/3](https://maroonersrock.com/2012/02/interview-chris-wilson-path-of-exile-part-1-of-3)).
- 측정: 시뮬레이션의 룬·스킬 픽률 분산과 승률 상위 빌드 점유율(밸런스 목표치).

## P2. 더 들어갈 것인가 — 압박 속의 결정
- 런의 긴장은 줄어드는 자원과 다음 던전의 위험 사이에서 **계속 갈지** 판단하는 데서 나온다. 결정은 재앙 직전에 가장 흥미롭다.
- 위험은 부상·사망 누적이 아니라 **런 자원과 진행(던전 단위 체크포인트, 전멸 = 런 종료)**으로 만든다.
- 근거: decisions §2 진행 체크포인트(m00732), §1 전멸 규칙, 부상·사망 없음(m00710) · Darkest Dungeon Bourassa: "We're testing player's abilities to think at the point of disaster", "Yes you're underpowered, yes you're vulnerable, focus up, think carefully" ([Game Developer](https://www.gamedeveloper.com/business/-i-darkest-dungeon-i-designing-for-despair-and-kicking-you-when-you-re-down)).

## P3. 결정 전에 읽힌다 — 정보는 충분히, 화면은 단정하게
- 플레이어가 커밋하기 전에 적의 의도, 피해 예측, 효과 범위가 보인다. 결과에 놀라는 것은 정보 부족 때문이어서는 안 된다.
- 피드백은 일관된다. 같은 종류의 효과는 같은 방식으로 보인다.
- 정보량과 화면 혼잡은 충돌한다 → **층위형 툴팁 + 용어집**으로 기본 화면은 단정하게, 깊이는 한 단계 아래에.
- 근거: Pillars of Eternity Josh Sawyer: "our UI does not give great feedback … inconsistent feedback", "it would probably flood the UI with checks constantly" ([RPG Codex](https://rpgcodex.net/content.php?id=10493)) · Sid Meier: "erring on the side of providing the player with too much information", "interesting decisions … involve some kind of tradeoff" ([GDC 2012](https://www.gamedeveloper.com/design/gdc-2012-sid-meier-on-how-to-see-games-as-sets-of-interesting-decisions)) · Into the Breach Justin Ma: "sacrifice cool ideas for the sake of clarity every time".

## P4. 네 명, 전원 직접 조작 — 모든 선택에 대가가 있다
- 파티 4명을 직접 조작한다. 모든 선택지(스킬, 룬, 이동, 보상)는 상황에 따라 우열이 바뀌며, 항상 이기는 선택지는 결함이다.
- 근거: decisions §1 "파티 최대 4명, 전원 직접 조작"(확정) · Sid Meier: "If a player always chooses the first from among a set of three choices, it's probably not an interesting choice".

## 하지 않을 것 (anti-goals)
- 그라인드화(확정 실패 모드), 런 간 골드·장비·재화 이월(누적 금지, 확정), 부상·사망·스트레스 누적(m00710, 폐기 확정), 숨겨진 수치로 인한 패배.

## 채택하지 않은 레퍼런스 요소
- Darkest Dungeon의 스트레스/고통 시스템, 영구 사망, 명단 소모: 부상·사망 없음과 스트레스 누적 폐기(확정)에 반함.
- Darkest Dungeon의 "영웅이 명령을 따르지 않음": 전원 직접 조작(확정)에 반함.

## 사용자 확인이 필요한 취향 질문 (추천안 포함)
1. 난이도 태도 — 추천: "어렵지만 공정하게(패배 원인이 항상 화면에 있었다)". DD식 "모두를 위한 게임은 아니다"까지는 가지 않는다.
2. 던전 중 후퇴/조기 귀환 — 추천: 던전 사이에서만 "귀환 vs 계속"을 고르게 해 P2의 결정점을 만든다(던전 내 후퇴는 없음).
3. EA 콘텐츠 규모(유닛·적·던전 수) — 추천: 시뮬레이션 기반 밸런스가 돌기 시작한 뒤 측정치로 정한다.
