# Game Code Changeability Guidelines (추가/삭제/수정이 쉬운 게임 코드 규칙)

> 목적: `game01P` (C++23 / SDL3 / EnTT, 엔진 미사용) 코드에서 **기능 추가·삭제·수정 비용**을 낮게
> 유지한다.
>
> **검증 가능성 계약**: 모든 규칙은 (a) 숫자 또는 파일 경로로 표현되는 FAIL 조건, (b) 그 조건을
> 재현하는 명령, (c) 판정 어휘(PASS/FAIL/N-A/UNVERIFIED)를 가진다. 주관적 형용사("무관한",
> "거대한", "불일치")만으로 FAIL을 선언할 수 없다.
>
> **실행 주체**: 리뷰어는 [`agents/changeability-reviewer.md`](agents/changeability-reviewer.md)
> 절차에 따라 이 문서의 R1~R33을 판정하고, 각 finding을 `file:line` + 심각도 + 최소 수정으로 기록한다.
> R21~R30은 DOOM 3 유산(id Software, S14)을 현대화한 규칙이고, §2.2의 R31~R33은 "로그·관측·주석을 코드에
> 상주시킨다"(S15~S19)는 실무 합의를 규칙화한 것이다. DOOM 3 → 현대 대응표는 Appendix B다.
>
> **이 문서 자체의 시험**: §6 캘리브레이션 하네스(`scripts/verify-changeability.sh`)가 §5 명령을
> 픽스처 코퍼스(`tests/changeability/fixtures/`)에 대해 실행하고 기대값과 대조한다. §5와 스크립트가
> 어긋나면 스크립트가 실패한다 — 문서가 아니라 스크립트가 진실이다.

---

## 0. 근거 소스 (Source Ledger)

| ID | 소스 | 이 문서가 가져온 것 |
| ---- | ------ | -------------------- |
| S1 | Robert Nystrom, *Game Programming Patterns* — <https://gameprogrammingpatterns.com/> (Architecture, Component, Event Queue, Type Object, State, Service Locator, Singleton) | 결합 = 한쪽을 이해/수정하려면 다른 쪽을 알아야 하는 상태. Component/Event/Type Object로 결합 절단, Singleton 남용 경계, 증분적 설계. |
| S2 | Casey Muratori, *Semantic Compression* — <https://caseymuratori.com/blog_0015> | 직접·단순하게 먼저 쓰고, 반복이 증명된 뒤에만 압축. 선제적 추상화는 비용. |
| S3 | Mike Acton, *Data-Oriented Design and C++* (CppCon 2014) — <https://cppcon.org/third-keynote-2014/> | 프로그램의 목적은 데이터 변환. 하나가 있으면 여럿이 있다. |
| S4 | Noel Llopis, *Games From Within* — <https://gamesfromwithin.com/data-oriented-design/>, <https://gamesfromwithin.com/my-fear-of-middleware> | 툴킷 > 프레임워크. 프레임워크가 아키텍처를 지배하지 않게. |
| S5 | Jason Gregory, *Game Engine Architecture* — <https://www.gameenginebook.com/> | 계층형 구조(platform → core → resource/render → gameplay)와 "엔진은 게임에 독립적". |
| S6 | 데이터 주도 설계: Unity *Architect your code with Scriptable Objects* <https://unity.com/how-to/architect-game-code-scriptable-objects> + *Data-Driven Design* <http://www.gamearchitect.net/Articles/DataDrivenDesign.html> | 수치·콘텐츠는 데이터 자산에. 이벤트 채널로 결합 제거. 복잡한 로직의 softcoding은 금지. |
| S7 | Deletability: M. Seemann *Decouple to delete* <https://blog.ploeh.dk/2022/11/21/decouple-to-delete/>, K. Sutton *Deletability* <https://kellysutton.com/2017/05/29/deletability.html> + UE5 Game Feature Plugin 격리 | 좋은 아키텍처의 실증 지표는 "기능 하나를 몇 개 파일 삭제로 제거 가능한가". |
| S8 | C++ Core Guidelines (Stroustrup/Sutter) — R.11, I.11, E.6 — <https://isocpp.github.io/CppCoreGuidelines/> | 자원 소유권은 RAII 타입에. `new`/`delete` 직접 호출과 소유 raw 포인터 금지(R15). |
| S9 | Glenn Fiedler, *Fix Your Timestep!* — <https://gafferongames.com/post/fix_your_timestep/> + S5 | 고정 스텝 시뮬레이션과 결정론. 프레임레이트에 결과가 흔들리면 회귀를 재현할 수 없다(R16). |
| S10 | 스키마 진화: Microsoft *API design guidance — versioning* <https://learn.microsoft.com/en-us/azure/architecture/best-practices/api-design> + Unity ScriptableObject 버전 필드 관행 | 데이터에 스키마 버전 필드를 두고, 키 삭제/이름 변경 시 마이그레이션 경로를 남긴다(R17). |
| S11 | SDL3 문서(<https://wiki.libsdl.org/SDL3/FrontPage>) + S5 | 렌더/오디오/입력은 메인 스레드 친화. 스레드 친화성은 선언되어야 검증 가능(R18). |
| S12 | C++ Core Guidelines E.2/E.6 + Google C++ Style (exception policy) | 실패는 조용히 삼키지 않는다. 정책(반환 코드/예외/assert)을 하나 정하고 테스트 이음새를 남긴다(R19). |
| S13 | 이 레포 계약: `src/ecs/systems/ISystem.h`(`name()`, `update()`), `SystemConcept` + S2/S5 | 새 타입은 계약을 만족해야 하고, 등록되지 않은 코드는 존재하지 않는 코드다(R20). |
| S14 | id Software **DOOM 3** GPL 소스 — <https://github.com/id-Software/DOOM-3> (commit `a9c49da`) | 변경 용이성을 실제로 만든 메커니즘: 이름→타입 등록(`neo/game/gamesys/Class.h:110`), decl+spawnargs(`neo/framework/DeclManager.cpp:806`, `neo/game/Game_local.cpp:3041`), cvar/cmd 단일 표면(`neo/framework/CVarSystem.h:217`, `CmdSystem.h:80`), pak search path(`FileSystem.cpp:1002`), 타입 이벤트 테이블(`gamesys/Event.h:53`), 플랫폼 경계(`sys/sys_public.h:537`). 반대로 버릴 것: 매크로 RTTI, 전역 파사드, `idDict`/`idStr` 전면 사용, 전역 힙(`idlib/Heap.h:75`), 정수 인덱스 이벤트, `neo/d3xp/` fork-by-copy. |
| S15 | Fabian Giesen(ryg), *The care and feeding of worker threads, part 1* — <https://fgiesen.wordpress.com/2013/02/17/care-and-feeding-of-worker-threads-part-1/> (RAD Telemetry) | "I manually instrument the source code to tell Telemetry when certain events are happening" — 계측을 소스에 직접 심고 데이터를 모아 나중에 시각화한다. 로그는 문제가 생긴 뒤 붙이는 것이 아니라 코드의 일부다(R31). 동시성 버그 추적에서 로깅을 더 넣는 것이 실제 수단이라는 농담이 관행인 이유이기도 하다. |
| S16 | Elan Ruskin (Valve), GDC *Forensic Debugging: How to Autopsy, Repair, and Reanimate a Release-built Game* — <https://www.gdcvault.com/play/1014352/Forensic-Debugging-How-to-Autopsy> + GDC *Unified Telemetry* (Rainbow Six Siege) <https://www.gdcvault.com/play/1023200/> | 릴리스 빌드는 사후 진단(forensic) 대상이고, 로그·이벤트·스냅샷을 **한 곳에 모아 나중에 조회**할 수 있어야 해부가 된다. 로그 없는 릴리스는 원인 규명 불가 → R31(관측 상주). |
| S17 | Epic, *Logging in Unreal Engine* (`UE_LOG(Category, Verbosity, Message)`) — <https://dev.epicgames.com/documentation/unreal-engine/logging-in-unreal-engine> + 커뮤니티 정리 <https://unrealcommunity.wiki/logs-printing-messages-to-yourself-during-runtime-n5ifosqc> | 카테고리 + verbosity로 **상세 로그를 코드에 남겨 두고 필터로 제어**한다. 로그를 지우는 것이 아니라 레벨로 끈다 → R31(레벨 제어, 제거 금지), R33(카테고리=파일 역할). |
| S18 | Tracy profiler(`ZoneScoped`, `TRACY_ENABLE` 없으면 0으로 컴파일) — <https://github.com/wolfpld/tracy/blob/master/manual/tracy.md> + "cross-cutting concerns like logging, tracing, and profiling must be part of the code" <https://softwareengineering.stackexchange.com/questions/437027/> | 계측 코드는 남아 있고 빌드 플래그로 켜고 끈다. 시스템 경계마다 존/스팬을 두면 "지금 어디에 시간을 쓰는지"를 항상 확인할 수 있다 → R32. |
| S19 | Brian Kernighan — "The most effective debugging tool is still careful thought, coupled with judiciously placed print statements" (*Unix for Beginners*; 정리: <https://earthly.dev/blog/printf-debugging/>) + 주석 규율: J. Atwood *Code Tells You How, Comments Tell You Why* <https://blog.codinghorror.com/code-tells-you-how-comments-tell-you-why/>, A. Zucker *Useful Code Comments* "Never tell me what, sometimes tell me why, always tell me why not" <https://eddie.codes/posts/source-code-comments/> | 확인 수단(print/로그)은 적절히 배치된 것이지 나중에 붙이는 것이 아니다. 주석은 "무엇"이 아니라 "왜/왜 안 하는가"를 쓰고, 코드가 이미 말하는 것을 되풀이하면 시간이 지나 거짓말이 된다 → R33. |

공통 caveat: **성능 vs 추상화** — 결합 제거용 간접층(가상 함수, 서비스 로케이터)은 캐시 지역성을
깨뜨릴 수 있으므로 핫 루프에서는 S3 원칙이 우선한다(R13).

---

## 1. 이 프로젝트의 계약과 채택 상태 (리뷰 시 절대 위반 금지)

의존 방향 규칙:

- `core/`는 게임플레이도 `states/`도 `ecs/`도 모른다 (`src/core/`).
- `src/ecs/`는 `states/`를 모른다. `gameplay/`를 모른다.
- `states/`와 `gameplay/`는 `core/` + `ecs/`를 알 수 있다.

**채택 상태 표** — 경로가 없으면 `PASS`가 아니라 `N-A (미채택)`이며, 같은 리뷰의 부채(debt) 표에
한 줄로 기록한다. 존재하지 않는 경로를 근거로 PASS/FAIL을 선언하는 것은 금지한다.

| 계약 | 경로 | 상태 | 리뷰 시 처리 |
| ------ | ------ | ------ | ------------- |
| 레이어 | `src/core/`, `src/ecs/`, `src/states/` | 존재 | R7로 강제 (FAIL 가능) |
| 게임플레이 레이어 | `src/gameplay/` | 존재 (slice 1: 데이터 계층 + GameplayServices) | R7로 강제 (FAIL 가능): `core/`나 `ecs/`가 `gameplay/`를 include하면 여전히 FAIL. "미래 레이어" 표현은 더 이상 유효하지 않다 |
| 컴포넌트 | `src/ecs/components/*.h` (`F` 접두어) | 존재 | R2로 강제 |
| 시스템 | `src/ecs/systems/ISystem.h`, `SystemConcept` | 존재 | R1/R20으로 강제 |
| 팩토리 | `src/core/factories/` | **미생성(예정)** | R5는 `N-A (미채택)`. 단 `reg.create()`가 팩토리 밖에 나타나면 FAIL + 부채 표 1줄 |
| 수치 데이터 | `assets/data/` | 존재 (`assets/data/*.json`, `schema_version=1`) | R4는 여전히 "폴백 1개 + 같은 줄 `// fallback:` 마커"일 때만 PASS. 튜닝 비용 질문은 이제 예 — 리컴파일 없이 값을 바꾼다 |
| 서비스 주입 | `registry.ctx()` | 존재 | R6으로 강제 |
| 이벤트 | `src/core/events/FEventBus.h` (typed event) | 존재 | R3으로 강제 |
| 앱 흐름 상태 | `FBaseState` / `GameStateMachine` | 존재 | R11로 강제 (게임플레이 상태는 태그 컴포넌트) |
| 플랫폼 경계 | `src/core/` (SDL3 호출 허용 계층) | 존재 | R26으로 강제 (ecs/states/debug의 `SDL_*` 금지) |
| 자산 경계 | `src/core/AssetManager.h`, `src/core/ResourceManager.*` | 존재 | R23/R25의 **유일한 문자열 키·파일 IO 예외** (다른 파일은 줄에 `// boundary:` 표기 필요) |
| 데이터 자산 | `assets/data/` | 존재 | R22/R17은 이제 강제 검사다 (로드 시 스키마 검증, `schema_version` 확인). `N-A (미채택)`이 아니다 |
| 저장/스냅샷 | 없음 | **미생성(예정)** | R28은 `N-A (미채택)` + 부채 표 1줄 |
| 모듈 경계 | 단일 바이너리 (`src/main.cpp` 정적 링크) | 존재 | R29는 경계 구조체가 없으므로 `N-A` + 부채 1줄. 단 R30(fork-by-copy)은 즉시 FAIL 가능 |
| 로깅 표면 | `src/core/Logger.h` (`LOG_*` 매크로, spdlog, 콘솔 + `game01p.log`) | 존재 | R31로 강제 (`#if`로 감싸 삭제하지 않고 레벨로 제어) |
| 관측/디버그 뷰 | `src/debug/DebugOverlay.*`, Tracy `ZoneScoped*` (`src/core/Engine.cpp:28` 등) | 존재 | R32로 강제 (로그도 스팬도 없는 "침묵 시스템" 금지) |
| 주석 마커 | `// why:`, `// invariant:`, `// fallback:`, `// boundary:`, `// thread-affinity:`, `// 미결(design §N):` | 관례는 문서화됨(사용 0건) | R33으로 강제 (신규 파일에 역할 주석 + 왜 마커) |

---

## 2. 규칙 카탈로그 (R1~R33)

각 규칙 = **형태 → FAIL(측정 가능) → 확인(명령)**. 명령의 정본은 §5 / `scripts/verify-changeability.sh`다.

### R1. 한 파일 = 한 타입, 이름 = 파일명 (S1, S5)

- 형태: `src/ecs/systems/<Name>System.h`에 타입 하나. 파일 basename == 정의된 struct 이름.
- FAIL (측정): ① 한 파일에 `: public ISystem` 타입이 **2개 이상**, ② 파일 **400줄 초과**,
  ③ basename과 정의된 타입 이름이 다름, ④ `update()`가 `{FPosition, FVelocity, FCollider, FSprite, FLayer}`
  중 **3개 이상 서로 다른 컴포넌트군**을 변이.
- 확인: `scripts/verify-changeability.sh` R1 블록 (struct 개수/파일) + `wc -l src/ecs/systems/*.h`.

### R2. 데이터와 로직 분리 (S3, S4)

- 형태: 컴포넌트는 POD 필드만. 로직은 시스템에.
- FAIL (측정, 코드 라인만 — 주석의 단어는 세지 않는다): 컴포넌트에 ① `virtual` 선언,
  ② 사용자 선언 소멸자(`~Type(`), ③ 소유 raw 포인터 필드(`T* p{nullptr}`).
  시스템 FAIL: 시스템이 프레임 간 누적되는 가변 멤버를 가진다(멤버가 매 `update()` 후에도 살아남음).
- 확인: `rg -n "^\s*(virtual\s|~[A-Za-z_][A-Za-z0-9_]*\s*\(|[A-Za-z_:<>]+\s*\*\s*[A-Za-z_]\w*\s*\{\s*nullptr)" src/ecs/components/`.
  소유 raw 포인터는 **타입 표기를 가리지 않는다**: `std::string* cachedLabel{nullptr}`도 ③ 위반이다(단일 단어
  타입만 잡는 패턴은 F2 결함이었다).

### R3. 시스템 간 직접 호출 금지 → typed event (S1 Event Queue, S6)

- 형태: 발행 `bus->publish<T>()` / 지연 `queueFrame<T>()`, 구독 `subscribe<T>()`.
- FAIL (측정): 시스템 헤더가 **다른 구체 시스템 헤더**를 include. 계약 헤더 `ISystem.h`는 제외.
  또는 `Engine.cpp`/파이프라인이 시스템 A를 만들어 B에 주입.
- 확인: `rg -n '#include "ecs/systems/[A-Za-z]+System\.h"' src/ecs/systems/ | rg -v 'ISystem\.h'`.

### R4. 하드코딩 금지 — 수치는 데이터 (S5, S6)

- 형태: 튜닝 가능 값(데미지·속도·쿨다운·스폰 수·에셋 경로)은 `assets/data/`에서. 코드 폴백은 **1개**,
  같은 줄에 `// fallback:` 마커 필수(마커가 없으면 FAIL, 줄이 다르면 FAIL).
- FAIL (측정): `src/ecs/`·`src/states/`·`src/gameplay/`에서 `// fallback:` 미표기 소수 리터럴(`0.35f`) 또는
  에셋 경로 리터럴(`"player_idle.png"`). 수학/엔진 상수(`tau`, `MAX_DELTA_TIME_SECONDS`, 컨테이너 기본값)는 제외.
- FAIL (반대 방향, softcoding): 분기/룰 로직 자체를 JSON 스크립트로 밀어넣기.
- `assets/data/`가 존재하므로: PASS는 "폴백 1개 + 같은 줄 `// fallback:` 마커"일 때만. 튜닝 비용 질문은 예(리컴파일 없이 값을 바꾼다).

### R5. 엔티티 생성은 팩토리 단일 경로 (S1, S7)

- 형태: 팩토리 struct가 컴포넌트 조합을 소유. `src/core/factories/`가 생기기 전에는 `N-A (미채택)`.
- FAIL (측정): 팩토리 디렉터리 밖에서 **엔티티 생성**이 일어남 — `reg.create()`, `reg.emplace<T>(e, ...)`,
  `emplace_or_replace<T>(e, ...)`, 교체 없이 새 엔티티에 컴포넌트를 붙이는 모든 형태.
  `registry.ctx().emplace<T>()`는 R6이 승인한 서비스 주입이므로 **R5 위반이 아니다**(첫 인자가 엔티티가 아님).
- 확인: `rg -n "\.create\(\)|\.emplace[_a-z]*<[^>]*>\(\s*[A-Za-z_]\w*\s*[,)]" src/ | rg -v '[\\/]factories[\\/]'`.

### R6. 전역 상태(싱글톤) 금지 (S1 Service Locator, S4)

- 형태: `registry.ctx().emplace<T>()` / `find<T>()` / `get<T>()`.
- FAIL (측정): ① `static T& (instance|Instance|getInstance)` 패턴, ② **이름과 무관하게** 헤더의
  비`constexpr` 가변 네임스페이스 스코프 변수(`int counter = 0;`), ③ 들여쓰기된 `extern` 포함 모든 `extern` 선언,
  ④ 함수 지역 `static` 비const 객체.
- 확인: §5 R6a~R6d. `staticInstanceCount` 같은 이름도 잡힌다(이름 기반 탐지 금지).
  R6④ 패턴은 들여쓰기된 `static` 중 `const`/`constexpr`과 함수 정의를 제외한 모든 것을 후보로 잡는다
  (`^\s+static\s+` → `rg -v 'static\s+(const|constexpr)'` → `rg -v 'static\s+…\('`). 함수 지역 static은
  선언 줄만 보고 판정하지 않고 함수 본문을 읽어 확정한다.

### R7. 의존 방향과 계층 경계 (S5)

- FAIL (측정): ① `src/core/` 또는 `src/ecs/`가 `#include "<gameplay|states>/..."`,
  ② 같은 두 디렉터리의 **코드**에서 `Player`/`Enemy` 식별자 사용. 주석 전용 언급은 제외.
- 확인: §5 R7a/R7b (모든 주석 제거 후 판정 — 전체 행 및 줄 끝 주석 모두. 줄 끝 주석의 `Player`
  단어가 오탐이 되지 않게 한다).

### R8. 새 기능은 "추가"로 가능해야 한다 (S1 Type Object, S3)

- 형태: 새 종류 = 새 파일/데이터 항목 추가. 기존 조건문 수정 0.
- FAIL (측정): 같은 enum에 대해 `switch`/`if-else` 분기가 **서로 다른 파일 3개 이상**에 존재해
  열거자 1개 추가 시 3개 파일을 고쳐야 함. "곳"은 항상 **파일 수**로 센다(케이스 라벨 수가 아니다).
- 확인: `rg -l "case\s+\w+::|==\s*\w+::" src/` — **접두어 무관**(`EWeapon`이든 `Weapon`이든)이고
  `case`와 `==` 양쪽을 세며, 파일 수는 enum별로 계산한다(`scripts/verify-changeability.sh` R8).

### R9. 삭제 가능성(deletability) (S7, S6 Game Feature 격리)

- 판정 질문: 이 기능을 되돌리면 **기능 자체 파일 삭제 + 등록 1줄 제거**로 끝나는가?
- FAIL (측정): 되돌리기 diff가 `{기능이 추가한 파일}` ∪ `{등록 지점 ≤1}` 밖의 파일을 수정해야 함.
  `#ifdef`/플래그/주석 처리된 죽은 코드를 기능 파일 밖에 남겨야 하면 FAIL.
- 확인: `rg -n "#\s*(ifdef|ifndef|if defined)" src/ecs src/states src/core | rg -v "pragma|guard"`.

### R10. 추상화는 반복이 증명된 뒤 (S2)

- 형태: 인터페이스/템플릿/정책 클래스는 구현체 2개 이상 또는 사용처 3곳 이상일 때만.
- FAIL (측정): 어떤 추상화의 **구현체 ≤1 그리고 사용처 ≤2**. 이름이 `I`로 시작하는지로 판정하지 않는다
  (`struct ISystem`처럼 `struct`로 선언된 인터페이스도 포함).
- 확인: §5 R10 (pure virtual 선언 추출 → 파생/사용처 수 대조). 정적 분석이 있으면 `clang-query` 사용.

### R11. 상태는 명시적이고 검증 가능 (S1 State)

- 형태: 게임플레이 상태 = 배타적 태그 컴포넌트 + 전이 표 문서.
- FAIL (측정): 한 엔티티의 상태를 나타내는 상호배타 `bool is*` 필드가 **2개 이상** (이름 무관, 타입 기반),
  또는 `FBaseState` 계층에 게임플레이 상태 추가, 또는 전이 표 문서 부재.
- 확인: `rg -n "\bbool\s+(is|has|can|should)[A-Z]\w*" src/ecs/components/` + 전이 표 존재.

### R12. 단일 진실 공급원 + 국소 변경 (S4, S6)

- FAIL (측정): ① 같은 이름·같은 값의 상수가 **2개 이상 파일**, ② diff가
  `docs/reviews/<slug>/contract.md`의 in-scope 목록 밖 파일을 수정, ③ 한 변경이 요구하는 수정 지점 ≥3파일.
- 확인: `git diff main...HEAD --name-only`와 contract의 차집합, 상수 중복은 §5 R12.

### R13. 최적화는 측정 후 (S3, S4)

- 형태: 핫 패스만 데이터 지향(연속 배열, `view` 순회, 캐시 친화 배치).
- FAIL (측정): 핫 패스의 알고리즘/자료구조 교체가 `pass-3.md`에 **전/후 수치 쌍**(ZoneScoped 존 이름 +
  빌드 구성 명시)을 남기지 않음. `ZoneScoped` 존재는 증거가 아니다.
- 확인: 변경 파일의 `ZoneScoped` + `docs/reviews/<slug>/pass-3.md`의 수치 쌍 존재 여부.

### R14. 새 파일 추가 = 등록 부담 최소 (S2, S7)

- FAIL (측정): 새 파일 1개 추가에 ① 코드 목록, ② 빌드 스크립트, ③ 레지스트리 중 **2개 이상**의
  수동 수정이 필요. 현재 CMake는 `GLOB_RECURSE CONFIGURE_DEPENDS`(`CMakeLists.txt`)이므로 파일 추가는 0회.
- 확인: `rg -n "GLOB_RECURSE|add_executable" CMakeLists.txt` + 부트스트랩 커밋에 새 파일만 추가되는지.

### R15. 소유권은 RAII 타입에 (S8)

- 형태: 자원 소유는 RAII(`SDLDeleter.h`, `std::unique_ptr`)로. 관찰은 비소유 포인터/참조.
- FAIL (측정): `new T`/`delete p` 직접 호출, 소유 raw 포인터 필드(`T* p{nullptr}`), 소유 raw 포인터를
  `registry.ctx()`에 넣고 해제 지점이 없음(leak), 수명이 소유자보다 긴 대여.
- 확인: `rg -n "new\s+[A-Za-z_]|delete\s+[A-Za-z_(]|[A-Za-z_:<>]+\s*\*\s*[A-Za-z_]\w*\s*\{\s*nullptr" src/ |
  rg -v '[\\/](factories|imgui|vendor|third_party)[\\/]'` — 서드파티는 제외하고 경로 필터는
  Windows 구분자(`\`)도 잡아야 한다(F13).

### R16. 결정론과 재현성 (S9)

- 형태: 고정 스텝 시뮬레이션, 고정 순회 순서, 시드 고정 RNG.
- FAIL (측정): 시뮬레이션 경로에서 ① `unordered_map`/`unordered_set` 순회가 결과에 영향,
  ② 시드 없는 `std::random_device`/`mt19937{random_device{}()}` 매 프레임 생성, ③ 벽시계 시간(`time(nullptr)`,
  `steady_clock`) 사용, ④ 시스템 등록 순서에 결과가 의존하는데 순서가 문서화되지 않음.
- 확인 (자동): `rg -n "random_device|\.detach\(\)|time\(nullptr\)|steady_clock" src/ecs src/core` → 후보 0 기대.
  `unordered_map`/`unordered_set`은 **include/필드 선언만으로는 위반이 아니므로** 자동 판정하지 않고
  `NOT CHECKED (manual)`로 넘긴다(순회 지점을 읽어 결과 의존성을 판정).

### R17. 데이터 스키마 진화 (S10)

- 형태: 설정/세이브 데이터에 `schema_version` 필드, 키 삭제/이름 변경 시 마이그레이션 경로.
- FAIL (측정): ① 새 설정 파일에 `schema_version` 없음, ② 필수 키 추가/삭제를 기존 데이터 파일의
  갱신 없이 머지(읽기 실패), ③ 세이브 포맷 변경에 버전 상승 또는 마이그레이션 함수 부재.
- 확인: `jq -e 'has("schema_version")' assets/data/*.json`. `assets/data/`가 존재하므로 이 규칙은
  `N-A (미채택)`이 아니라 강제 검사다(어느 파일이든 `schema_version`이 없으면 FAIL).
  `jq`가 없는 환경(Win Git Bash에서 관측됨)을 위해 `scripts/verify-changeability.sh`의 R17 검사는
  `command -v jq`로 분기해 rg 폴백을 쓴다 — 명령이 `command not found`로 조용히 통과하면 안 된다.

### R18. 스레드 친화성 (S11)

- 형태: SDL 렌더·오디오·입력과 entt registry는 메인 스레드. 작업 스레드는 선언된 순수 데이터만.
- FAIL (측정): `std::thread`/`std::async`/`detach()`가 ① 메인 스레드 소유 자원(renderer, registry, audio)을
  건드리거나 ② `// thread-affinity: <무엇을 어디서>` 선언이 없음.
- 확인: `rg -n "std::thread|std::async|\.detach\(\)" src/` → 히트가 있는 **파일마다**
  `// thread-affinity: <무엇을 어디서>` 선언이 있어야 한다(선언 없는 파일 = 위반). 시스템 등록 순서 표는
  `src/core/SystemManager.h`를 참조.

### R19. 실패는 삼키지 않는다 + 테스트 이음새 (S12)

- 형태: 실패는 로그/반환 코드/`assert` 중 하나로 표면화. 새 시스템에는 테스트 이음새를 남긴다.
- FAIL (측정): ① `catch (...)` 또는 빈 `catch`가 로그/에러 반환 없이 종료, ② 로드/파싱 실패가
  기본값으로 조용히 대체되고 로그 없음, ③ 새 시스템/규칙 추가에 대응하는 테스트 또는
  `tests/changeability/fixtures/` 항목 부재.
- 확인: `rg -n "catch\s*\(\s*\.\.\.|catch\s*\([^)]*\)\s*\{\s*\}" src/`.

### R20. 계약 적합성과 통합 (S13)

- 형태: 새 타입은 `ISystem`/`SystemConcept`을 만족(`name()`, `update()`)하고, **정확히 한 등록 지점**에
  등록되며, include가 해결되고, 헤더에 TU별 정의를 만들지 않는다.
- FAIL (측정): ① `: public ISystem`인데 `name()` 미정의(추상/미등록), ② 등록 지점 부재(dead-on-arrival),
  ③ 1차 include가 해결 불가(`#include "gameplay/Player.h"` 대상 디렉터리 없음), ④ 헤더의 비inline
  네임스페이스 스코프 변수(ODR), ⑤ 역할과 다른 디렉터리 배치(예: `src/ecs/ZzX.h`) 또는 basename과
  내용 불일치(자유 함수 `zzIconOf`가 `...State.h` 안에 있음).
- 확인: §5 R20 + `rg -n "struct [A-Za-z]+[^;{]*: *public ISystem" src/ecs/systems/`에서
  `name()` 존재 대조 + 등록 지점(`src/main.cpp`/`src/core/SystemManager.h`) 참조 확인.
  include 해결 규칙(F3): 1차 세그먼트 디렉터리가 `src/`에 없고 알려진 SDK 접두어
  (`entt`, `SDL3*`/`SDL2*`/`SDL*`, `imgui`, `glm`, `stb`, `spdlog`, `fmt`, `nlohmann`, …)도 아니면
  **미해결로 FAIL**한다 — `gameplay/`가 아직 없어도 `#include "gameplay/Player.h"`는 위반이다
  (과거 구현은 없는 디렉터리를 조용히 건너뛰어 이 케이스를 놓쳤다).

---

### 2.1 DOOM 3 유산 — 계승할 핵심, 현대화 규칙 (R21~R30)

근거는 id Software가 GPL로 공개한 **DOOM 3** 소스(`github.com/id-Software/DOOM-3`, commit `a9c49da`, S14).
목적은 2004년 코드를 베끼는 것이 아니라 **무엇이 변경을 쉽게 만들었는가**(메커니즘)만 뽑아 C++23/EnTT로
옮기고, 그 당시의 비용은 버리는 것이다. 기계적 대응표는 **Appendix B**.

DOOM 3에서 실제로 작동했고 그대로 계승할 6가지:

1. **이름→타입 등록**(`neo/game/gamesys/Class.h:110` `CLASS_DECLARATION`, `Game_local.cpp:3051`
   `cls->CreateInstance()`) — 데이터 파일이 새 엔티티 종류를 지목할 수 있다.
2. **콘텐츠는 코드 밖**(`neo/framework/DeclManager.cpp:806-823` `RegisterDeclType`,
   `neo/game/Game_local.cpp:3041` `spawnArgs.SetDefaults(&def->dict)`) — DLL 재컴파일 없이 밸런스 변경.
3. **단일 튜닝/명령 표면**(`neo/framework/CVarSystem.h:217`, `CmdSystem.h:80`) — 모든 서브시스템을 같은
   콘솔·설정 경로로 만진다.
4. **가상 파일시스템 오버레이**(`neo/framework/FileSystem.cpp:1002` search path walk, `:462`
   `AddGameDirectory`) — 모드/확장이 데이터 오버레이로 붙는다.
5. **타입 있는 이벤트 표면**(`neo/game/gamesys/Event.h:53`, `neo/game/Entity.cpp:109` `EVENT(...)` 테이블) —
   C++·콘솔·스크립트가 같은 연산 이름을 쓴다.
6. **플랫폼 경계 한 계층**(`neo/sys/sys_public.h:537` `idSys`, `neo/sys/win32|posix/`) — 렌더/사운드/입력이
   하나의 이음새로 포팅된다.

DOOM 3에서 **버릴** 것(그대로 옮기면 위반): 매크로 RTTI와 static 초기화 자기등록, 전역 `common->` 파사드,
`idDict`/`idStr` 전면 사용, 전역 힙 리다이렉트(`neo/idlib/Heap.h:75` `operator new`), 정수 인덱스 이벤트
디스패치와 `void*` 인자 패킹, `neo/d3xp/`(140파일)가 `neo/game/`(136파일)을 통째로 복사한 fork-by-copy.

규칙 충돌 시 R1~R20이 우선한다. R23은 R4를 대체하지 않는다(R4 = 리터럴 탐지, R23 = 문자열 조회 경계).

### R21. 등록은 한 지점, 자기등록 매크로·static 금지 (S14)

- 원본: `CLASS_DECLARATION`(`Class.h:110`)이 static `idTypeInfo`를 만들고 그 생성자 부작용이 전역
  리스트에 자기등록한다(`Class.cpp:65`). 타입 번호는 시작 시 순회로 부여된다(`Class.cpp:392-402`).
- 형태: 새 타입/시스템은 **명시적 등록 1곳**(기존 등록 지점 또는 `register_all()`)에 등록한다.
  등록은 코드에서 보이는 호출이어야 하고 static 초기화 순서에 의존하지 않는다.
- FAIL (측정): ① 등록 DSL 매크로 신설(`#define ..._DECLARATION|_PROTOTYPE|_REGISTER`), ② 자기등록 static
  타입 객체(`static XType XType;` 계열), ③ 같은 타입의 등록 지점 2곳 이상(수동).
- 확인: `rg -n '^\s*#\s*define\s+[A-Z_]*(DECLARATION|PROTOTYPE|REGISTRATION|REGISTER)\b' src/`(기대 0),
  `rg -n '^\s*static\s+[A-Z]\w*\s+\w*(Type|Meta|Registry)\s*[({;]' src/`(기대 0).
- 현재: 0 → PASS.

### R22. 콘텐츠·밸런스는 코드 밖 (S14, S6)

- 원본: decl 타입/폴더 등록(`DeclManager.cpp:806-823`), def dict 병합(`Game_local.cpp:3041`),
  `spawnclass` → 클래스 생성(`Game_local.cpp:3044-3051`).
- 형태: 엔티티/무기/적/스폰 정의는 `assets/data/`의 자산이고, 로더가 **스키마 검증 후** 타입 있는
  컴포넌트로 변환한다. 코드에는 스키마·기본값·변환만 남는다.
- FAIL: 새 엔티티/무기를 추가할 때 밸런스 수치나 구성 키를 코드에 넣어야만 동작하거나, 자산에만 있고
  검증되지 않는 키(오타가 런타임에야 발견).
- 확인: `assets/data/*.json`이 존재하고, 각 테이블이 `src/core/data/FContentLoader.cpp`에서
  선언된 `FContentSchema`로 검증된다(§5 R22). 이 슬라이스에서 `FContentField` 배열은
  `src/gameplay/data/*Content.h`에, 스키마 객체(`kUnitSchema`/`kSkillSchema`)는
  `src/gameplay/data/FContentRegistry.cpp`에 있다. 심각도 High: 새 콘텐츠마다 코드 수정 = 3대 비용 위반.

### R23. 문자열 백은 로더 경계 안에만 (S14 idDict 유산)

- 원본: `neo/idlib/Dict.h:240-252`(`GetString`/`atof`/`atoi`), `neo/game/Entity.h:122` `spawnArgs`,
  전역 키/값 풀(`Dict.cpp:52-57`).
- 형태: 문자열 키 조회와 `std::map<std::string,std::string>` 류 범용 dict는 **자산 로더/직렬화 경계
  파일에서만** 허용한다. 런타임은 타입 있는 구조체/컴포넌트를 쓴다.
- FAIL (측정): 런타임 코드의 문자열 키 조회(`args.GetInt("health")`), 런타임 상태로 보유한
  `unordered_map<std::string, std::string>`.
- 경계 예외: `src/core/AssetManager.*`, `src/core/ResourceManager.*`, 또는 해당 줄에 `// boundary:` 표기.
- 확인: `rg -n '\b(Get|Set)(Int|Float|Bool|String)\s*\(\s*"|(std::map|std::unordered_map)\s*<\s*std::string\s*,\s*std::string' src/`
  (경계 예외 후 기대 0). 현재: 0 → PASS.

### R24. 튜닝·디버그 표면은 하나 (S14 cvar/cmd 유산)

- 원본: `AddCommand`(`CmdSystem.h:80`), `Register`/`Find`/`SetCVarString`(`CVarSystem.h:217-224`),
  일괄 등록(`neo/game/gamesys/SysCmds.cpp:2295-2304`).
- 형태: 전역 디버그·계측·시각 토글은 한 debug/config 서비스에 등록해 config 자산 또는 콘솔에서 조작한다.
  시스템/상태/core에 흩어진 토글 멤버는 금지.
- FAIL (측정): `$SRC/ecs/systems`, `$SRC/states`, `$SRC/core`에 `bool (show|debug|draw|enable)X` 필드.
- 범위 밖(위반 아님): 컴포넌트의 **per-entity** 시각 플래그(예: `src/ecs/components/FDebugPrimitive.h`
  `drawCollider{true}` — 이건 데이터다), 함수 파라미터(`bool enableFileLog = true`) — 패턴이 줄 시작에
  고정된 이유다. 같은 줄 다중 선언은 수동 판정.
- 확인: `rg -n '^\s*bool\s+(show|debug|draw|enable)[A-Z]' src/ecs/systems src/states src/core`(기대 0).
  현재: 0 → PASS. 등록 지점 단일성은 수동(NOT CHECKED).

### R25. 자산·경로 해석은 한 곳 (S14 FileSystem 유산)

- 원본: `searchpath_t *searchPaths`(`FileSystem.h:406`), pak 해시 조회(`FileSystem.cpp:1002-1019`),
  `AddGameDirectory`(`:462`), `AddZipFile`(`:1400`).
- 형태: 파일 열기·경로 조립·오버레이 우선순위는 `AssetManager`/`ResourceManager` 한 곳. 게임플레이/시스템
  코드는 논리 이름(asset id)만 다룬다.
- FAIL (측정): 경계 밖의 `std::ifstream`/`std::ofstream`/`fopen`/`std::filesystem::path|exists`.
- 확인: `rg -n 'std::ifstream|std::ofstream|fopen\s*\(|std::filesystem::(path|exists)' src/`(경계 예외 후
  기대 0). 현재: 0 → PASS. 오버레이 우선순위 결정론은 수동.

### R26. 플랫폼 경계는 한 계층 (S14 sys 유산 + S5)

- 원본: `class idSys`(`sys_public.h:537-573`) + 전역 `sys`, 플랫폼별 `neo/sys/win32|posix|osx/`,
  그리고 같은 기능의 자유 함수 이중 API(`Sys_*`).
- 형태: SDL3/OS API는 `src/core/`(플랫폼 경계)에서만 쓴다. `ecs/`, `states/`, `debug/`는 SDL 타입을 모른다.
- FAIL (측정): `$SRC/ecs`, `$SRC/states`, `$SRC/debug`에서 `SDL_*` 사용.
- 확인: `rg -n '\bSDL_[A-Za-z]' src/ecs src/states src/debug`(baseline 제외 후 기대 0).
- 현재: `src/ecs/systems/DebugPrimitiveRenderSystem.h`, `src/ecs/systems/SpriteRenderSystem.h`가 SDL을 직접
  호출 → **PRE-EXISTING 2건**(baseline 등재; 렌더 호출을 core Renderer 서비스로 옮기면 삭제). 신규 위반은
  차단하며 심각도는 Blocker다.

### R27. 이벤트·명령은 타입 있는 구조체 (S14 Event 유산 + S1 Event Queue)

- 원본: `D_EVENT_MAXARGS 8`(`Event.h:46`), 전역 연속 `eventnum`(`Event.cpp:135-136`), 인자를 `int`로
  패킹한 `idEventArg`(`Class.h:56-72`), `ProcessEventArgPtr`(`Event.cpp:532`), 클래스별 `eventMap[]`
  (`Class.cpp:167-194`).
- 형태: 이벤트는 타입 있는 struct + `FEventBus`이고 핸들러는 **타입**으로 연결된다. 이름/정수 인덱스
  디스패치 금지, 인자는 포인터 패킹이 아니라 필드로 전달.
- FAIL (측정): ① `publish("damage", …)` 같은 문자열 이름 디스패치, ② `void* payload` 이벤트 인자,
  ③ `ProcessEventArgPtr` 식 인덱스 디스패치.
- 확인: `rg -n '(publish|dispatch|emit|send)\s*\(\s*"|void\s*\*\s*(args|payload|data)\b|ProcessEventArgPtr' src/core/events src/ecs`
  (기대 0). 현재: 0 → PASS. R3는 "직접 호출 금지"를, R27은 "이벤트의 형태"를 본다.

### R28. 스냅샷은 컴포넌트 단위 (S14 SaveGame 유산 + S10)

- 원본: RTTI에 박힌 `Save`/`Restore` 함수 포인터(`Class.h:110-116`), `WriteObject`/`WriteDict`
  (`SaveGame.h:48-69`), `savefile->WriteDict(&spawnArgs)`(`Entity.cpp:628`).
- 형태: 저장/복원은 컴포넌트 열거로 생성하고, 포맷에 `schema_version`을 둔다(R17).
- FAIL (측정): 기능/클래스마다 손으로 쓴 `Save`/`Restore` 쌍, 버전 없는 스냅샷 포맷.
- 확인: `rg -n 'void\s+(Save|Restore)\s*\(|\b(SaveGame|RestoreGame)\s*&' src/`(기대 0). 저장 레이어가
  없으면 `N-A (미채택)` + 부채 표 1줄(현재 그 상태).

### R29. 모듈 경계는 좁고 버전 있는 인터페이스 (S14 GetGameAPI 유산)

- 원본: `GAME_API_VERSION = 8`(`neo/game/Game.h:326`), raw 인터페이스 포인터 14개를 담은 `gameImport_t`
  (`:328-346`), `gameExport_t`(`:348-353`), export 심볼 1개(`neo/game/Game.def`), 로드 + 버전 검사
  (`Common.cpp:2633-2675`), 게임 쪽 전역 재주입(`Game_local.cpp:98-101`).
- 형태: 단일 바이너리면 정적 링크 + 좁은 모듈 인터페이스. 경계 구조체를 만든다면 **버전 필드 필수**,
  소유권은 `unique_ptr`/참조로 명시, 전역 재주입 금지.
- FAIL (측정): 버전 없는 `*Import/*Export/*API` 구조체, 소유 raw 포인터 전달, DLL 로드/`GetProcAddress`
  기반 핸드오프.
- 확인: `rg -n '(struct|class)\s+\w*(Import|Export|API|Api)\w*|DLL_(Load|GetProcAddress)|GetGameAPI' src/`
  - 소유권 수동 판정. 경계가 없으면 `N-A (미채택)`(현재 그 상태).

### R30. 변형은 데이터/기능 조합으로, 레이어 복사 금지 (S14 d3xp 유산)

- 원본: `neo/d3xp/`(140파일)가 `neo/game/`(136파일)을 거의 그대로 복사하고 같은 `GetGameAPI`를 export한다.
  `neo/game/Game.h:241`의 주석(`// FIXME: this interface needs to be reworked but it properly separates
  code for the time being`)이 당시 경계가 불완전했음을 인정한다.
- 형태: 확장/모드는 자산 오버레이 + 기능 플래그/컴포넌트 조합으로 만든다. 같은 레이어/모듈을 통째로
  복제해 변형을 만들지 않는다.
- FAIL (측정): 같은 basename 파일이 2경로에 있고 **양쪽 모두 ≥40줄이며 짧은 쪽 줄의 ≥60%가 긴 쪽에도
  존재**(실질 복사). 3줄짜리 포워딩 shim(`src/core/Logger.h` vs `src/debug/Logger.h`)은 위반 아님.
  `#ifdef` 변형 분기는 R9가, variant 전용 디렉터리 신설/자산 오버레이 부재는 수동 판정이 맡는다.
- 확인: `bash scripts/verify-changeability.sh`의 R30 라인(기대 0). 현재: 0 → PASS.

---

### 2.2 관측과 주석 — 항상 확인할 수 있는 코드 (R31~R33)

세 규칙의 공통 전제: **디버깅은 나중에 도구를 붙이는 일이 아니라 코드를 쓸 때 같이 쓰는 일**이다.
근거(S15~S19): ryg는 소스에 직접 계측을 심어 이벤트를 모아 나중에 시각화한다고 썼고(S15), Valve의
포렌식 디버깅 특강은 릴리스 빌드를 사후 해부하려면 로그가 있어야 한다고 말하며(S16), Epic의 `UE_LOG`는
상세 로그를 코드에 남긴 채 카테고리·verbosity 필터로 제어한다(S17). Tracy의 `ZoneScoped`는 빌드 플래그가
없으면 0으로 컴파일되는 계측이고, 로깅·추적·프로파일링은 코드의 일부일 수밖에 없는 cross-cutting
concern이다(S18). Kernighan은 "가장 효과적인 디버깅 도구는 신중한 사고와 적절히 배치된 print 문"이라
썼고(S19), 주석은 무엇이 아니라 왜/왜 안 하는지를 쓰는 것이 관행 합의다(S19).

기존 규칙과의 경계: R19는 "실패를 삼키지 않는다", R31은 "그 실패·상태 전이가 **로그로 남는가**"를 본다.
R24는 전역 토글을 한 곳에 모으라는 규칙이고, R32는 관측 호출(로그/스팬)을 **경계마다 두라**는 규칙이다
(충돌 아님: R24의 대상은 전역 `bool show*` 필드, R32의 대상은 관측 호출이다).

### R31. 로그는 코드에 상주, 제어는 레벨로 (S15, S16, S17, S19)

- 형태: 상태 전이·자원 로드 결과·실패 경로 옆에 `LOG_*` 한 줄. 끄는 방법은 **레벨**(`logger->set_level`,
  config 자산)이다. 로그를 `#ifdef`/`#if`로 감싸 빌드에서 지우거나, 디버깅이 끝났다는 이유로
  로그 줄을 삭제하지 않는다.
- FAIL (측정, repo-wide): ① `LOG_*` 호출이 전처리기 조건 블록 **안**에 있음(기대 0),
  ② 로그 레벨을 컴파일 플래그로만 결정(`#ifdef DEBUG` + `LOG_` 조합).
- FAIL (측정, 증분): 리뷰 범위에서 **새로 추가된** 시스템/상태/로더 `.h`/`.cpp`에 관측 호출
  (`LOG_*` 또는 `ZoneScopedN`) 0건.
- 확인: §5 R31 (전처리기 블록 추적 = `awk`; 증분은 `git diff --name-only`). 현재 repo-wide ① = 0 → PASS.
- 심각도: High(릴리스 빌드가 해부 불가해지면 회귀 원인이 사라진다).

### R32. 관측 없는 "침묵 시스템" 금지 (S15, S18, S16)

- 형태: `ISystem` 구현 파일은 최소 1개의 관측 지점을 가진다 — `ZoneScopedN("<시스템 이름>")`(시간) 또는
  `LOG_*`(상태/실패). 이름은 `name()` 반환값과 같게 둔다(오버레이·프로파일러·로그를 같은 이름으로 읽는다).
  상태 요약을 화면에서 보려면 `src/debug/DebugOverlay.cpp`의 렌더 경로에 그 시스템 항목을 추가한다.
- FAIL (측정): `src/ecs/systems/*.h` 중 `public ISystem`이며 `LOG_[A-Z]+(`도 `ZoneScopedN`도 없는 파일.
- 확인: §5 R32 루프(기대 0). 현재 `src/ecs/systems/*.h`의 구현 5개(`AnimationSystem.h`, `CollisionSystem.h`,
  `DebugPrimitiveRenderSystem.h`, `MoveSystem.h`, `SpriteRenderSystem.h`)는 **모두 `ZoneScopedN`을 이미
  보유** → 위반 0(baseline 불필요). 단 이 5개는 `LOG_*` 상태 로그가 0건이므로, 디버그 뷰 배선 여부는
  수동 판정으로 보고한다. 신규 시스템 파일은 예외 없이 즉시 FAIL이다.
- 심각도: Medium(이해/디버깅 비용). 신규 시스템에서 2개 이상 연속 위반이면 High로 올린다.

### R33. 주석은 왜를 남긴다 — 짧고 명확하게 (S19, S17)

- 형태: ① 새 파일 상단에 역할 1줄(트레이싱 시 어떤 카테고리/시스템인지), ② 비자명한 결정마다
  `// why:` 1~2줄(왜 이 순서인가 / 왜 안전한가 / 무엇이 깨지면 안 되는가), ③ 우회·폴백은
  `// fallback:`, 경계 예외는 `// boundary:`, 불변식은 `// invariant:`, 스레드는 `// thread-affinity:`,
  미확정 값의 출처는 `// 미결(design §N):`(design 문서의 어느 절이 이 값을 아직 정하지 않았는지).
  금지: 다음 줄 코드를 그대로 되풀이하는 주석(`// player position` + `player.position = …`).
- FAIL (측정): ① 잡음 주석 — 주석의 내용어(≥3자, 불용어 제외)가 2개 이상이고 **모두** 다음 코드 줄의
  토큰에 존재하며 `why|invariant|fallback|boundary|thread-affinity|미결(` 마커가 없음(기대 0),
  ② 증분: 새로 추가된 `src/` 파일에 `//` 주석이 0줄(역할 주석 부재).
- 확인: §5 R33 (잡음 주석 탐지기는 `awk`; 증분은 `git diff --diff-filter=A`).
- 심각도: Medium(이해 비용 — 4대 질문의 "이해 비용"에 직접 걸린다).

---

## 3. 변경 비용 — 리뷰가 항상 숫자로 답하는 4대 질문

`docs/reviews/<slug>/pass-2.md`에 기록한다. 미채택 경로는 `NO(미채택)`으로 답하고 부채 표에 남긴다.

1. **추가 비용**: 새 기능이 기존 파일을 몇 개 수정했는가? (기대: 0~1 + 등록 1줄)
2. **삭제 비용**: 되돌리면 파일 몇 개가 사라지고 참조 몇 곳을 지우는가? (기대: 기능 파일 + 등록 1줄)
3. **튜닝 비용**: 밸런스 수치를 컴파일 없이 바꿀 수 있는가? (기대: 예, `assets/data/`. 현재 예 — `assets/data/*.json`에서 리컴파일 없이 바꾼다)
4. **이해 비용**: 이 코드를 이해하려 다른 파일 몇 개를 읽어야 하는가? (3개 초과 = 결합 냄새)

---

## 4. 심각도와 판정 함수

| 심각도 | 정의 | 예 |
| -------- | ------ | ---- |
| **Blocker** | §1 계약(레이어/소유권/전역상태/컴포넌트 순수성) 위반, 또는 빌드 불가 | R7 의존 방향, R6 싱글톤, R2 컴포넌트 가상함수, R15 소유 raw 포인터, R20 include 미해결 |
| **High** | 다음 기능 추가 시 필연적으로 여러 파일 수정 | R8 switch 산개, R3 시스템 직접 호출, R5 산개 생성, R16 비결정론, R18 무선언 스레드 |
| **Medium** | 국소 개선으로 해결 | R1 비대 시스템, R10 과잉 추상화, R12 중복 상수, R4 하드코딩, R11 bool 상태 |
| **Low** | 스타일/가독성 | 네이밍, 선언 배치, 포맷 |

**판정은 심각도 집계의 함수다** (재량 없음):

- `RESET` — `core/`·`ecs/`·의존 방향·소유권 계층에 Blocker가 1개 이상, 또는 빌드 불가.
- `REWORK` — Blocker가 1개 이상이거나 High가 1개 이상, 또는 UNVERIFIED > 0.
- `GROWING` — Blocker = 0, High = 0, UNVERIFIED = 0. Medium/Low는 부채 표로 넘긴다.

**증거 적격성**: `PASS`는 그 규칙의 판정 근거가 되는 **증상 라인**을 인용해야 한다. 인용한
`file:line`이 증상을 포함하지 않으면(예: R7 PASS의 근거로 `#pragma once` 줄을 인용) 카운트가
0이어도 그 PASS는 무효이며 판정은 `REWORK`다. 자동 판정 규칙(R1~R9, R11, R12a, R14~R16a, R18~R21,
R23~R27, R30~R32, R33a)의 PASS는 하네스 출력 라인을 함께 인용해야 한다.

**심각도 매핑(R21~R30)**: Blocker — R26(플랫폼 계약 위반), R30(레이어 복사), R29(소유권 없는 경계);
High — R21(등록 산개/자기등록), R23(런타임 문자열 백), R27(이름 디스패치·`void*` 인자), R22(자산이 아닌
코드가 밸런스 소유); Medium — R24, R25, R28.

**심각도 매핑(R31~R33)**: High — R31(관측 삭제로 릴리스 진단 불가); Medium — R32(침묵 시스템),
R33(잡음 주석/역할 주석 부재). R31~R33은 **증분 적용**을 원칙으로 한다: 기존 부채는 baseline + 부채 표
지표로 보고하고, 리뷰 범위에서 새로 생긴 침묵·잡음만 차단한다.

빌드/테스트 통과는 승인 근거가 아니다(P1의 신호일 뿐). P2(구조)가 이 문서의 판정이며,
`DEV_WORKFLOW.md`의 Review-Cycle Branch Flow와 `docs/reviews/_template.md`를 따른다.

---

## 5. 검증 명령 묶음 (정본)

정본 구현은 `bash scripts/verify-changeability.sh`(`--calibrate`로 픽스처 대조)다. 아래는 그 사람이
읽는 형태이며, 두 가지가 다르면 **스크립트가 맞다**.

세 가지 공통 필터가 모든 명령에 붙는다(모든 규칙에 적용):

- **서드파티 제외**: `src/imgui`, `vendor/`, `third_party/`, `src/factories/`는 위반 집계에서 빠진다.
- **경로 구분자**: Windows `C:\...` 출력을 포함해 `\\`와 `/` 둘 다 처리한다(F13).
- **PRE-EXISTING baseline**: `tests/changeability/baseline.txt`에 등재된 `(규칙, 파일)`은 차단하지 않고
  개수로만 보고된다. baseline 등재는 부채 표 1줄 + 사유가 필수이며, **green을 위해 baseline을
  추가하는 것은 그 자체가 finding**이다.

```bash
SRC=src

# R1a 한 파일 한 ISystem (각 파일 1개)   R1b basename == 타입명   R1c 파일 ≤400줄 (기대: 0 위반)
for f in $SRC/ecs/systems/*.h; do
  echo "$f: $(rg -o 'struct [A-Za-z_]+[^;{]*: *public ISystem' "$f" | wc -l)"
done

# R2  컴포넌트 순수성 (주석 제외, 코드 라인만; 기대: 0)
#     소유 raw 포인터는 타입 표기를 가리지 않는다 (F2)
rg -n "^\s*(virtual\s|~[A-Za-z_][A-Za-z0-9_]*\s*\(|[A-Za-z_:<>]+\s*\*\s*[A-Za-z_]\w*\s*\{\s*nullptr)" $SRC/ecs/components/

# R3  시스템 간 include (ISystem.h 제외; 기대: 0)
rg -n '#include "ecs/systems/[A-Za-z]+System\.h"' $SRC/ecs/systems/ | rg -v 'ISystem\.h'

# R4  게임플레이 리터럴 ('// fallback:' 같은 줄과 수학/엔진 상수는 허용; 기대: 0)
rg -n "[-+]?[0-9]+\.[0-9]+f|\"[a-z0-9_/]+\.(png|wav|json)\"" $SRC/ecs/systems $SRC/states \
  | rg -v '//\s*fallback:' | rg -v '\b(tau|kPi|kEpsilon|MAX_[A-Z0-9_]+)\b'

# R5  팩토리 밖 '엔티티 생성'만 (기대: 0). ctx().emplace<T>()는 서비스 주입이므로 제외 (F4)
rg -n "\.create\(\)|\.emplace[_a-z]*<[^>]*>\(\s*[A-Za-z_]\w*\s*[,)]" $SRC/ | rg -v '[\\/]factories[\\/]'

# R6a 이름 기반 싱글톤 (기대: 0)   R6b 헤더의 가변 전역 (기대: 0)   R6c extern (기대: 0)
# R6d 함수 지역/멤버 static 비-const (기대: 0) — const는 전역 불변 테이블이므로 허용 (F7)
rg -n "static\s+[A-Za-z_:<>]+\s*[*&]?\s*(instance|Instance|getInstance)" $SRC
rg -n "^(static\s+)?(int|float|double|bool|std::\w+|[A-Z]\w+)\s+\w+\s*(=[^=]|\{)" $SRC --glob '!**/*.cpp'
rg -n "^\s*extern\s" $SRC
rg -n "^\s+static\s+" $SRC | rg -v 'static\s+(const|constexpr)' | rg -v 'static\s+[A-Za-z_:<>]+[\s*&]+[A-Za-z_]\w*\s*\('

# R7a 경계 include (기대: 0)   R7b 경계 어휘 (줄 끝 주석 포함 제거 후; 기대: 0)
rg -n '#include\s+"(gameplay|states)/' $SRC/core $SRC/ecs
rg -n "\b(Player|Enemy)\b" $SRC/core $SRC/ecs | sed -E 's|//.*$||'

# R8  enum 분기 파일 수 (기대: enum당 <3 파일) — 접두어 무관, `case`와 `==` 양쪽
rg -l "case\s+\w+::|==\s*\w+::" $SRC/ | sort

# R9  죽은 #ifdef (기대: 0)
rg -n "#\s*(ifdef|ifndef|if defined)" $SRC/ecs $SRC/states $SRC/core | rg -v "pragma|guard"

# R10 추상화 (pure virtual 선언 → 구현체/사용처 수 대조) — 픽스처에 가짜 계층이 없어 수동 판정
rg -n "^\s*virtual\s+[^=;]+=\s*0\s*;" $SRC

# R11 bool 상태 (기대: 0) + 전이 표 존재
rg -n "\bbool\s+(is|has|can|should)[A-Z]\w*" $SRC/ecs/components/

# R12a 중복 상수: (이름,값) 쌍이 2개 이상 파일 (기대: 0)
rg --no-heading -o -r '$1=$2' '\b((?:k|K_)[A-Za-z0-9_]*)\s*=\s*([0-9]+\.?[0-9]*f?)' $SRC/ecs $SRC/core $SRC/states \
  | awk -F: '{print $NF"|"$1}' | sort -u | cut -d'|' -f1 | sort | uniq -d

# R12b 계약 밖 파일 (contract.md in-scope와 차집합)
git diff main...HEAD --name-only

# R14 빌드 등록 부담 (GLOB_RECURSE 여부)
rg -n "GLOB_RECURSE|add_executable" CMakeLists.txt

# R15 소유권 (서드파티 제외, 기대: 0)
rg -n "new\s+[A-Za-z_]|delete\s+[A-Za-z_(]|[A-Za-z_:<>]+\s*\*\s*[A-Za-z_]\w*\s*\{\s*nullptr" $SRC/ \
  | rg -v '[\\/](factories|imgui|vendor|third_party)[\\/]'

# R16a 비결정 요소 (기대: 0)   R16b unordered_* 순회 결과 의존은 수동(NOT CHECKED)
rg -n "random_device|\.detach\(\)|time\(nullptr\)|steady_clock" $SRC/ecs $SRC/core

# R18 스레드 친화성 (히트 파일마다 // thread-affinity: 선언 필요)
rg -n "std::thread|std::async|\.detach\(\)" $SRC/

# R19 삼킨 예외 (기대: 0)
rg -n "catch\s*\(\s*\.\.\.|catch\s*\([^)]*\)\s*\{\s*\}" $SRC/

# R20 include 해결 (1차 세그먼트 디렉터리 부재 = 미해결, 서드파티 접두어만 허용; 기대: 0)
#     + ISystem 파생 타입의 name() 존재, 등록 지점 1곳
rg -n '#include "' $SRC/ecs $SRC/core $SRC/states

# R21 등록 매크로 / 자기등록 static 타입 객체 (기대: 0)
rg -n '^\s*#\s*define\s+[A-Z_]*(DECLARATION|PROTOTYPE|REGISTRATION|REGISTER)\b' $SRC
rg -n '^\s*static\s+[A-Z]\w*\s+\w*(Type|Meta|Registry)\s*[({;]' $SRC

# R23 문자열 키 조회 / string,string bag (경계 예외 후 기대: 0)
rg -n '\b(Get|Set)(Int|Float|Bool|String)\s*\(\s*"|(std::map|std::unordered_map)\s*<\s*std::string\s*,\s*std::string' $SRC \
  | rg -v '[\\/]core[\\/](AssetManager|ResourceManager)\.[ch]' | rg -v '//\s*boundary:'

# R24 경계 계층에 흩어진 전역 토글 (기대: 0)
rg -n '^\s*bool\s+(show|debug|draw|enable)[A-Z]' $SRC/ecs/systems $SRC/states $SRC/core

# R25 경계 밖 파일 IO / 경로 조립 (경계 예외 후 기대: 0)
rg -n 'std::ifstream|std::ofstream|fopen\s*\(|std::filesystem::(path|exists)' $SRC \
  | rg -v '[\\/]core[\\/](AssetManager|ResourceManager)\.[ch]' | rg -v '//\s*boundary:'

# R26 경계 밖 플랫폼 API (baseline 제외 후 기대: 0)
rg -n '\bSDL_[A-Za-z]' $SRC/ecs $SRC/states $SRC/debug

# R27 이벤트 형태: 문자열 이름 / void* 인자 / 정수 인덱스 (기대: 0)
rg -n '(publish|dispatch|emit|send)\s*\(\s*"|void\s*\*\s*(args|payload|data)\b|ProcessEventArgPtr' $SRC/core/events $SRC/ecs

# R28 손으로 쓴 Save/Restore 쌍 (기대: 0)
rg -n 'void\s+(Save|Restore)\s*\(|\b(SaveGame|RestoreGame)\s*&' $SRC

# R29 버전 없는 경계 구조체 / DLL 핸드오프 (수동 대조, 기대: 0)
rg -n '(struct|class)\s+\w*(Import|Export|API|Api)\w*|DLL_(Load|GetProcAddress)|GetGameAPI' $SRC

# R30 fork-by-copy (basename 중복 + 양쪽 ≥40줄 + ≥60% 동일; 기대: 0)
bash scripts/verify-changeability.sh | rg 'R30'

# R31a 전처리기로 감싼 로그 (블록 추적; 기대: 0)
bash scripts/verify-changeability.sh | rg 'R31'
# 사람이 읽는 형태:
awk '/^[[:space:]]*#[[:space:]]*(if|ifdef|ifndef)/{d++} \
     /^[[:space:]]*#[[:space:]]*endif/{if(d>0)d--} \
     d>0 && /LOG_(TRACE|DEBUG|INFO|WARN|ERROR|CRITICAL)[[:space:]]*\(/{print FILENAME":"FNR": "$0}' \
  $(rg -l 'LOG_' src)

# R31b 증분: 새/수정 파일에 관측 호출이 있는가 (기대: 0 위반)
for f in $(git diff --name-only main...HEAD -- 'src/*.h' 'src/*.cpp'); do
  rg -q 'LOG_[A-Z]+\(|ZoneScopedN' "$f" || echo "R31 $f: no observation call"
done

# R32 침묵 시스템: LOG_도 ZoneScoped도 없는 ISystem 구현 (기대: 0)
# 현재 5개 시스템과 2개 렌더 시스템 모두 ZoneScopedN을 보유하므로 repo는 0이다.
bash scripts/verify-changeability.sh | rg 'R32'
for f in $(rg -l 'public ISystem' src/ecs/systems); do
  rg -q 'LOG_[A-Z]+\(|ZoneScopedN' "$f" || echo "R32 $f: silent system"
done

# R33a 다음 줄을 되풀이하는 잡음 주석 (기대: 0)
bash scripts/verify-changeability.sh | rg 'R33'

# R33b 증분: 새로 추가된 파일에 주석 0줄 (기대: 0 위반)
for f in $(git diff --name-only --diff-filter=A main...HEAD -- 'src/*.h' 'src/*.cpp'); do
  rg -q '^\s*//' "$f" || echo "R33 $f: no comment"
done

# 관측 지표(부채 보고용, FAIL 아님)
for f in src/ecs/systems/*.h; do
  printf '%s log=%s span=%s\n' "$(basename "$f")" "$(rg -c 'LOG_[A-Z]+\(' "$f" 2>/dev/null || echo 0)" "$(rg -c 'ZoneScoped' "$f" 2>/dev/null || echo 0)"
done

# 캘리브레이션 + 계약 스캔 (필수, 리뷰 기록에 출력 붙여넣기)
bash scripts/verify-changeability.sh --calibrate     # 픽스처 코퍼스 대조
bash scripts/verify-changeability.sh                 # src/ 계약 스캔 + NOT CHECKED / PRE-EXISTING 요약
```

**정밀도 주의(오탐 금지)**: ① 주석의 단어(`virtual`, `Player`)는 위반이 아니다 — R2/R7b는 전체 행 및
줄 끝 주석을 제거한다. ② `// fallback:` 같은 줄의 상수 1개는 허용이다. ③ `ISystem.h` include는 위반이
아니다. ④ `tau`, `MAX_DELTA_TIME_SECONDS` 등 수학/엔진 상수는 R4 위반이 아니다. ⑤ `case EWeapon::`처럼
범위 지정 enum 케이스 라벨을 "잘못된 라벨"로 보고하는 정적 분석기 오탐이 있다(§6 참조) — 분석기
출력은 후보일 뿐이며 파일을 읽어 확정한다. ⑥ 서드파티(`imgui`/`vendor`) 히트는 위반이 아니다.
⑦ baseline에 등재된 부채는 차단 대상이 아니지만 **개수로 계속 보고된다** — 0이 되면 baseline에서 지운다.
⑧ R24는 **줄 시작의 필드**만 본다. 함수 파라미터(`bool enableFileLog = true`)와 컴포넌트의 per-entity
시각 플래그는 위반이 아니다(전자는 산개한 표면이 아니라 인자이고, 후자는 데이터다). 같은 줄 다중 선언은 수동 판정.
⑨ R30은 basename 중복만 보지 않는다 — 양쪽 ≥40줄 + ≥60% 동일 조건을 함께 요구하므로 3줄짜리 포워딩
shim은 오탐이 아니다. 그 미만의 유사 중복은 수동 판정이며, 복사 레이어로 판정되면 Blocker다.
⑩ R31은 로그를 **지우지 말라**는 규칙이지 "모든 줄에 로그를 넣으라"는 규칙이 아니다. 레벨 기본값이
`trace`인 것은 정상이며(`src/core/Logger.h`), 로그가 많다는 이유로 FAIL이 되지 않는다.
⑪ R32의 "관측"은 `ZoneScopedN` 하나로도 충족된다. 프로파일러가 꺼진 빌드에서 스팬이 0으로 컴파일되는
것은 정상이며(그게 설계다), 컴파일 제외를 이유로 R31 위반으로 세지 않는다 — R31①은 `LOG_*`만 본다.
⑫ R33의 잡음 주석 탐지기는 "다음 코드 줄이 이미 말하는 문장"만 잡는다. `// why:` 계열 마커가 있거나
주석의 내용어가 코드 토큰에 다 없으면 대상이 아니며, 한국어 주석도 같은 규칙으로 판정한다(내용어 판정은
ASCII 토큰 기준이므로 한글 주석은 오탐이 나지 않는다 — 그 경우 수동 판정으로 내려간다).

---

## 6. 캘리브레이션 하네스 (이 지침 자체의 시험)

- 코퍼스: `tests/changeability/fixtures/` — **의도적으로 깨진** 코드. `src/` 밖이므로 CMake 글롭
  (`CMakeLists.txt`: `GLOB_RECURSE ... "src/*.h"`)에 들어가지 않고 빌드되지 않는다.
- 자동/수동 경계: 하네스는 자동 판정 가능한 규칙만 본다. 판정할 수 없는 규칙은 마지막에
  `NOT CHECKED (manual): R10, R13, R16b, R18(affinity), R19b, R21(등록 지점),
  R24(등록 단일성), R28(스냅샷), R29(경계 소유권), R30(조합 여부), R31(증분 관측 커버리지),
  R32(디버그 뷰 배선), R33(비자명 결정의 why 주석 필요성)` 형태로 **명시**된다
  (스크립트 출력이 정본이다).
  이 목록이 출력에 없으면 "돌리긴 했다"는 증거가 아니므로 리뷰를 시작하지 않는다.
  `OK (repo)`는 **자동 규칙에 한정한** 문장이며, NOT CHECKED 항목에 대한 PASS 근거가 될 수 없다(§7).
- baseline: `tests/changeability/baseline.txt`(형식 `R15 src/core/AudioManager.h <사유>`)는
  **기존 부채**를 차단 대상에서 빼되 개수로 보고한다. 신규 코드를 baseline에 넣는 것은 금지다.
  현재 등재: R15 `AudioManager`(miniaudio 수동 수명), R26 `DebugPrimitiveRenderSystem.h`와
  `SpriteRenderSystem.h`(SDL3 호출이 ECS 시스템 안에 있음 → core Renderer 서비스로 옮기면 삭제).
  R31~R33은 현재 등재 없음: 관측 호출은 유일하게 R26 부채인 두 렌더 시스템까지 포함해 이미 있고,
  `#if`로 감싼 로그와 잡음 주석은 0건이다(기준선을 만들어 초록불을 만드는 것 자체가 적발 대상).
- 같은 이유로 정적 분석에서도 제외한다: `.pi-lens.json`의 `ignore`에 `tests/changeability/fixtures/**`와
  `assets/data/zz_*.json`이 등록되어 있다. 픽스처에서 나오는 위반 알림(`ZzWeaponSwitchB.h`의
  `case EWeapon::` 라벨 = `labeled_statement`, `enum x{A::B}`/람다 중괄호 = 비트필드 오탐)은
  의도된 것이므로 실제 코드의 finding 개수에 포함하지 않는다.
- 픽스처 트리의 `.clangd`는 `-I.`/`-I..`/`-I../..`를 추가해 픽스처 간 include(`ecs/components/FZzWeapon.h`)를
  해석시킨다(없으면 clang이 `pp_file_not_found` + `unknown_typename` 연쇄 오류를 뿜어 리뷰 노이즈가 된다).
  픽스처 트리의 `.clang-tidy`는 `readability-uppercase-literal-suffix`, `readability-identifier-naming`,
  `performance-enum-size`, `readability-convert-member-functions-to-static`, `readability-magic-numbers`만
  끈다 — 픽스처의 literal `10.f`와 `kZz*` 이름은 **규칙 탐지 대상 데이터**라서 스타일 검사를 고칠 수 없다.
  이 디렉터리 밖에는 어떤 예외도 없다. 새 예외를 추가하면 `--calibrate`를 다시 돌려 규칙 기대값이
  그대로인지 확인한다.
- 알려진 엔진 한계(우리 코드 문제 아님): pi-lens는 `.h`를 C 문법으로 디스패치한다
  (`pi-lens dist/index.js` 확장자 맵: `c: [".c", ".h"]`, `cpp: [".cpp", ".hpp"]`). 그래서 `.h` 픽스처의
  C++ 구문 — `enum class EWeapon { … };`, `case EWeapon::Sword:` 라벨 — 이 `no-bit-fields`,
  `non-case-label-in-switch` 같은 C 규칙에 오탐된다(둘 다 `language: c`). 이 오탐은 의도된 픽스처에서만
  발생하며 `src/` 계약 스캔(`OK (repo)`)에는 나타나지 않는다. 픽스처를 `.hpp`로 개명하면 사라지지만,
  프로젝트 헤더 규칙(`src/**/*.h`)과 하네스/기록 참조를 깨뜨리므로 개명하지 않고 여기에 기록한다.
- 위 한계는 픽스처에만 국한되지 않는다. slice 1 리뷰에서 `src/` 파일에도 같은 오탐이 관측됐다:
  `src/core/data/FContentValue.h`의 `std::variant` 멤버에 `no-bit-fields` 오탐 3건(14·15·17행, 실제
  비트필드 0건), CWD 기준 상대 include(`assets/...`, `scripts/...`)를 쓰는 파일에 `file not found`
  연쇄 오류. 따라서 `OK (repo)` 스캔의 hit 개수는 finding 개수가 아니다 — 리뷰어는 규칙 패턴과 수동
  확인으로 판정하고, 오탐 근거를 기록한다(실측: `docs/reviews/gameplay-foundation/evidence/`,
  `pass-1.md`·`pass-2.md`).
- 픽스처를 지침/스크립트 개선 없이 `src/` 쪽으로 옮기거나 삭제하면 캘리브레이션이 무효가 된다.
- 각 픽스처는 소스 안에 `V<n> [R<rule>]` 주석으로 위반 번호를 달고 있다.
- 하네스: `scripts/verify-changeability.sh --calibrate`가 규칙별 기대 매치 수를 단언한다.
  실패하면 §5 명령과 규칙이 어긋난 것이므로 **지침 또는 스크립트를 먼저 고치고 리뷰를 시작한다.**
- 리뷰 기록에는 하네스 출력(마지막 두 줄 포함)을 붙여넣는다 — 이것이 리뷰어 캘리브레이션 증거다.
- 알려진 분석기 오탐(픽스처에서 확인됨, 지침 §5 정밀도 주의 ⑤): 범위 지정 enum `case` 라벨을
  `labeled_statement`로, `enum x{A::B}`/람다 중괄호를 비트필드로 보고하는 사례. 분석기는 후보 생성기다.

---

## 7. 리뷰 기록 요건 (pass-2.md 필수 항목)

리뷰어는 아래를 모두 남긴다. 항목이 빠지면 그 리뷰는 `REWORK`다.

- 레퍼런스 범위(예: `main..HEAD`, `git diff` 범위)와 변경 파일 목록 + 각 파일 LOC 변화.
- Rule-by-rule 판정 표: R1~R33 각각 `PASS / FAIL / N-A / UNVERIFIED` + 근거 `file:line`.
  `N-A`는 §1 채택 상태 표에 없는 경로에만 허용되며, 같은 리뷰의 부채 표에 1줄로 등록한다.
  자동 판정 불가로 선언된 규칙(`NOT CHECKED` 목록)은 `UNVERIFIED` 또는 수동 판정으로만 채울 수 있고,
  하네스가 `OK`를 냈다는 사실을 그 규칙의 근거로 쓸 수 없다.
- **PASS의 증거 적격성**: 인용한 `file:line`은 그 규칙이 금지하는 **증상을 실제로 포함**해야 한다.
  (예: R7 PASS의 근거로 `#pragma once` 줄을 인용하면 카운트가 0이어도 그 PASS는 무효 → `REWORK`.)
- **리뷰어 식별**: 기록 상단에 `reviewer: <agent-id / session>` + `implements: yes/no`를 명시한다.
  `implements: yes`면 그 판정은 자기 승인으로 간주되어 무효(다른 세션/리뷰어가 재판정).
- **§5 캘리브레이션 출력 원문**: `bash scripts/verify-changeability.sh --calibrate`의 마지막 두 줄 포함
  (리뷰어 캘리브레이션 증거). 픽스처 없이 수행된 리뷰는 무효다.
- 실행한 §5 명령과 원문 출력(잘라내지 않음). 명령을 실행하지 않고 판정한 규칙은 `UNVERIFIED`다.
  원문 출력은 `docs/reviews/<slug>/evidence/*.txt`로 커밋한다(하네스 출력 포함) — 요약만 적으면 증거가 아니다.
- 리뷰어가 diff 밖 파일(`src/main.cpp` 등 등록 지점)을 근거로 인용했으면 그 근거는 실패 처리된다
  (계약 파일 안에서 증명해야 한다).
- `UNVERIFIED` 목록: 왜 판정할 수 없었는지 + 무엇이 증명하면 해소되는지.
- 변경 비용 4대 질문(§3)의 **숫자** 답변.
- 판정(§4 함수로 도출)과 삭제 가능성 문장, Blocker/High 개수.
- 최소 침습(constructive·source-plausible) finding: 각각 "이 diff에서 이 줄이 X면 Y 문제" 수준의 구체성.
  추측성 대규모 리팩터 제안은 finding으로 인정하지 않는다.
- 리뷰어는 **빌드를 실행하지 않는다**(`DEV_WORKFLOW.md` → Builds Are Human-Run). 빌드/체크 스크립트는
  프로젝트 소유자가 돌리며, 리뷰어는 `build gate: PENDING (owner-run)` + 실행할 명령만 남긴다.
  빌드 결과가 없으면 그 gate는 `UNVERIFIED`이고 PASS/FAIL이 아니다. 파일만 읽는 검사
  (`scripts/verify-changeability.sh`, §5 `rg` 묶음, `git diff --check`)는 허용된다.
- Teach-back: P1 구현을 3줄로 요약하고 통합 위험을 1개 이상 본인이 지적.
- 자기 승인 금지: 리뷰어가 해당 슬라이스를 구현했다면 다른 리뷰어/세션이 판정한다.

## Appendix A. 픽스처 → 규칙 → 기대값

| 픽스처 | 규칙 | 기대 |
| -------- | ------ | ------ |
| `ecs/systems/ZzComboSystem.h` | R1, R3, R4, R5, R9, R13, R20 | ISystem 파생 3개, 구체 시스템 include 1, 리터럴 4, `.create()` 1, `#ifdef` 1, name() 누락 |
| `ecs/components/FZzBad.h` | R2, R11, R12 | virtual 2줄 + 소유 포인터(`std::string*`), `bool is*` 2 |
| `ecs/components/FZzGood.h` | — (음성 대조군) | R2 = 0 (주석의 "virtual" 단어는 위반 아님) |
| `ecs/components/FZzStats.h` | R11 | `bool is*` 4 |
| `ecs/ZzKitUseA.h`, `ecs/ZzKitUseB.h`, `core/ZzKitUseC.h` | R12a | 같은 `kMax` 값이 3파일(중복 상수) |
| `ecs/components/FZzWeapon.h` | R8 | enum `EWeapon` (분기 3파일 산개) |
| `ecs/systems/ZzWeaponSwitchB.h`, `states/ZzWeaponSwitchC.h` | R8 | 파일 수 ≥3 |
| `ecs/systems/ZzSoloAbstraction.h` | R10 | 구현체 1 (`struct` 선언, `class I` 아님) |
| `ecs/ZzStateLeak.h` | R7 | `states/` include 2 |
| `core/ZzLeak.h` | R6, R7, R12, R20 | 싱글톤 2, 가변 전역 1, 게임플레이 include 1, 중복 상수, include 미해결. R4 = 0 (R4 음성 대조군) |
| `core/ZzOwnership.h` | R15 | `new` 2, 소유 raw 포인터 |
| `ecs/systems/ZzNoOrderSystem.h` | R6d, R16 | 함수 지역 static 1 + 비결정 요소 3 |
| `assets/data/zz_bad_config.json` | R17 | `schema_version` 없음 |
| `core/ZzThread.h` | R18 | 스레드 2, 친화성 선언 없음 |
| `core/ZzError.h` | R19 | `catch (...)` 1 |
| `ecs/ZzRegMacro.h` | R21 | 등록 매크로 1 + 자기등록 static 타입 객체 2 |
| `ecs/components/FZzDictBag.h` | R23 | 문자열 키 조회 2(`GetInt`/`GetFloat`) + `string,string` bag 1 |
| `ecs/components/FZzToggles.h` | R24 | `bool show/debug/draw*` 3 (컴포넌트지만 토글 이름을 재현) |
| `core/ZzAssetIo.h` | R25 | `ifstream`/`filesystem::path` 3 |
| `debug/ZzSdlLeak.h` | R26 | `SDL_*` 3 |
| `ecs/systems/ZzStringEventSystem.h` | R27 | `void* payload` 1 + 문자열 `publish("…")` 1 (+ 시그니처 1 = 3) |
| `ecs/systems/ZzSaveRestoreSystem.h` | R28 | 수동 `Save`/`Restore` 2 |
| `core/ZzMonolithBoundary.h` | R29 | 버전 없는 `ZzEngineImport` 구조체 1 |
| `ecs/ZzVariantCopy.h` + `debug/ZzVariantCopy.h` | R30 | 48줄/48줄, 98% 동일 줄 = 복사 레이어. **음성 대조군**: `src/core/Logger.h` vs `src/debug/Logger.h`(3줄)는 미탐지 |
| `core/ZzGuardedLog.h` | R31 | `#if` 블록 안의 `LOG_*` 2 (로그를 빌드에서 지우는 형태) |
| `ecs/systems/ZzSilentSystem.h` | R32 | `public ISystem` + `name()`/`update()` + `LOG_*` 0 + `ZoneScopedN` 0 = 침묵 시스템 1 |
| `core/ZzWhatComment.h` | R33 | 잡음 주석 1(`// player position` + `player.position = …`). **음성 대조군**: 같은 파일의 `// why:` 주석은 미탐지, `ecs/components/FZzGood.h`도 0 |

R26/R24는 실제 `src/` 부채와 분리해 캘리브레이션한다: 픽스처가 규칙 패턴을 재현하고, `src/` 쪽 R26 히트
2개는 baseline으로만 처리된다(위 §6 baseline 항목).

## Appendix B. DOOM 3 메커니즘 → 현대 대응 (S14)

각 행 = "DOOM 3에서 변경을 쉽게 만든 것 → game01P의 현대적 형태 → 현대화해도 살아남아야 하는 불변식 →
버릴 것". 마지막 열은 이 대응을 리뷰에서 강제하는 규칙 번호다.

| DOOM 3 메커니즘 (근거) | 현대 대응 (C++23/EnTT) | 살아남아야 하는 불변식 | 버릴 것 | R |
| ------ | ------ | ------ | ------ | --- |
| `idClass` + `CLASS_DECLARATION` (`neo/game/gamesys/Class.h:110`, 자기등록 `Class.cpp:65`) | EnTT registry + 컴포넌트 타입, 명시적 등록 함수 1곳 | 데이터(이름)가 새 종류를 지목 가능하고, 등록되지 않은 타입은 없다 | 매크로 RTTI, static 자기등록 순서 의존, `typeNum` 범위 검사 | R21, R20 |
| decl + spawnargs (`neo/framework/DeclManager.cpp:806`, `Game_local.cpp:3041`) | `assets/data/` 자산 → 스키마 검증 → 타입 있는 컴포넌트 | 밸런스·구성을 리컴파일 없이 바꿀 수 있다 | 런타임 문자열 읽기, 전역 `spawnArgs` 스크래치 dict | R22, R4 |
| `idDict` (`neo/idlib/Dict.h:240`, `Entity.h:122`) | dict는 로더 경계 안에서만, 런타임은 struct | 확장에 스키마 이주가 필요 없다 | 문자열 전면 사용, 전역 문자열 풀(`Dict.cpp:52-57`) | R23 |
| cvar + cmd (`neo/framework/CVarSystem.h:217`, `CmdSystem.h:80`) | 단일 debug/config 서비스 + config 자산 | 모든 튜닝·디버그가 한 표면에서 조작·스크립트 가능 | 전역 cvar 객체, cheat 플래그를 유일한 보안 경계로 | R24 |
| pak search path (`neo/framework/FileSystem.cpp:1002`, `:462`) | `std::filesystem` + 자산 매니페스트(마운트 순서) | 모드/확장 = 데이터 오버레이, 우선순위 결정론 | 손수 만든 링크드 리스트, 전역 `fs_*` 루트 | R25 |
| 이벤트 테이블 (`neo/game/gamesys/Event.h:53`, `Event.cpp:532` 인덱스 디스패치) | `FEventBus` 타입 이벤트 | C++·콘솔·스크립트가 같은 연산을 지목 | 정수 인덱스 디스패치, `void*`/`int` 패킹 인자, 클래스별 전체 크기 `eventMap` | R27, R3 |
| `idList`/`idHashTable` (`neo/idlib/containers/List.h:384`) | `std::vector`/`std::span`/`unordered_map` + `std::pmr` | 직렬화·네트워크에 필요한 결정론적 순회 | 재할당 복사 컨테이너, 자체 해시, hot path의 `idStr` | R16 |
| `idSaveGame` (`neo/game/gamesys/SaveGame.h:48-69`) | 컴포넌트 스냅샷 + `schema_version` | 상태가 하나의 모델에서 생성되고 이주 가능하다 | 클래스별 수동 `Save`/`Restore` | R28, R17 |
| `gameImport_t`/`GetGameAPI` (`neo/game/Game.h:326-353`) | 정적 링크 + 좁은 버전 인터페이스 | 게임 계층 교체 가능, 표면이 작고 버전이 있다 | 소유권 없는 raw 포인터 구조체, 전역 재주입 | R29 |
| `sys` 플랫폼 계층 (`neo/sys/sys_public.h:537`) | `core/` 플랫폼 계층 + SDL3 | OS 코드는 한 모듈에만, 나머지는 플랫폼 불가지 | 이중 API(class + 자유 함수 `Sys_*`), 전역 `sys` | R26, R7 |
| `common->` 파사드 (`neo/framework/Common.h:213`) | 주입된 서비스 + Logger | 로깅·실패 출력이 균일하고 비침습적 | 전역 특권 객체, fatal 문자열 에러를 제어 흐름으로 | R6, R19 |
| 전역 힙 (`neo/idlib/Heap.h:75` `operator new` 리다이렉트) | 표준 할당자 + `std::pmr` + sanitizer | 할당이 관측·검증 가능 | `#define new`, 수동 `Mem_Alloc`/`Mem_Free` 짝 | R15 |
| `neo/d3xp/` fork-by-copy (140 vs 136 파일, 같은 `GetGameAPI`) | 자산 오버레이 + 기능/컴포넌트 조합 | 변형이 기존 파일을 복제하지 않는다 | 통째로 복사한 레이어, variant 분기 `#ifdef` | R30, R9 |

이 표는 DOOM 3를 **읽는 순서**도 제공한다: 위에서부터 `Class.h` → `DeclManager.cpp`/`Game_local.cpp` →
`Dict.h` → `CVarSystem.h`/`CmdSystem.h` → `FileSystem.cpp` → `Event.h` → `List.h` → `SaveGame.h` →
`Game.h` → `sys_public.h` → `Common.h` → `Heap.h` 순으로 보면 "변경을 쉽게 만든 골격"이 순서대로 나오고,
마지막 행(`neo/d3xp/`)에서 그 골격이 무너진 대가를 볼 수 있다.
