# 참고 분석 브리프: Pillars of Eternity / Divinity: Original Sin → 우리 게임의 픽셀 에셋

두 게임의 공식 스크린샷 31장을 보고 **글로 된 규칙**을 뽑았다. 목표는 두 게임의 그림을 닮게 만드는 것이 아니라,
그 게임들이 한 화면에서 일관되게 읽히는 이유를 64x64 픽셀아트로 옮기는 것이다.
아트 방향은 **픽셀아트 유지**다 (사람 확인). `style.md` 의 금지 사항(페인터리, 3D 렌더 등)은 그대로 유효하다.

## 0. 사용 규칙 (CLAUDE.md 규칙 13)

- 스크린샷은 저작권물이다. 저장소에 커밋하지 않는다. 출처는 `reference/sources.json`, 로컬 사본은 `python scripts/fetch_references.py` 로 `reference/local/` (git 무시)에 받는다.
- 생성 도구에는 `create_image_pro` 의 스타일 입력(`style_image_url`, `style_copy` 는 `color_palette`, `shading` 만)으로만 넘긴다 (사용자 지시, 2026-11-02). 인페인트 배경, img2img, 앵커, `spec.references` 로는 쓰지 않는다. 그 밖에 반영하는 것은 이 문서의 글 규칙이다.
- 특정 캐릭터, 건물, 아이콘의 형태를 그대로 옮기지 않는다. 가져오는 것은 실루엣 규칙, 재질 처리, 빛과 색의 구조 같은 일반 원리다.
- 두 게임의 얼굴 표현은 따르지 않는다. 사람 파티원은 `style.md` 대로 얼굴이 없다.

## 1. 수집 결과

Steam 의 공식 홍보 스크린샷 31장 (PoE 12, Divinity: Original Sin 7, Divinity: Original Sin 2 12). 홍보 문구 이미지 1장(poe_00)은 제외하고 30장을 쓴다.
각 장의 태그와 한 줄 메모는 `reference/sources.json`.

| 용도 | 해당 이미지 |
|---|---|
| 유닛 | dos2_00, dos2_06, dos2_08, dos2_09, dos1_00, dos1_02, dos2_07 |
| 몬스터 | poe_03 (거미), poe_05 (거인), poe_11 (늑대), dos2_05 (상어), dos2_10 (짐승) |
| 배경 (바닥, 벽, 지형) | poe_01~11, dos1_00~06, dos2_01~04 |
| 오브젝트 | poe_01, 04, 06~10, dos1_01, dos2_01, 02, 04, 07, 11 |
| 효과 (이번 범위 아님) | poe_03, poe_11, dos1_03, 05, 06, dos2_01, 07, 10, 11 |
| UI (이번 범위 아님) | dos1_00, 01, 04, dos2_05, 08~11 |

## 2. 측정

화면 가장자리(UI)를 잘라낸 중앙부 기준. 스크린샷에는 유닛과 이펙트가 섞여 있고 우리 값은 불투명 픽셀만 잰 것이라 **대략적인 비교**다.
캐릭터 생성 화면, 에디터, 분할 화면은 제외했다 (PoE 11장, Divinity 12장).

| | 평균 밝기 | 상위 5% 밝기 | 거의 검정(<0.10) 픽셀 | 평균 채도 |
|---|---|---|---|---|
| Pillars of Eternity | 0.19 | 0.44 | 30% | 0.34 |
| Divinity (1, 2) | 0.29 | 0.60 | 11% | 0.38 |
| **우리 바닥, 벽, 배경** | 0.14 | 0.32 | | 0.39 |
| **우리 오브젝트** | 0.23 | 0.50 | | 0.31 |
| **우리 유닛** | 0.26 | 0.67 | | 0.48 |

읽는 법:
- 우리 환경은 **이미 PoE 만큼 어둡다.** 더 어둡게 만들 필요는 없다.
- PoE 의 어두운 장면은 어둡기만 한 게 아니라 **밝은 곳이 정해져 있다**: 횃불, 크리스탈, 모닥불, 발광 룬 석판, 그리고 유닛. 나머지는 값 범위가 좁다.
- 우리 유닛은 환경보다 확실히 밝고 채도가 높아 분리된다 (`style.md` 의 "유닛이 가장 밝게"와 일치).

우리 현재 유닛 8종의 일관성 (`style_fingerprint.py` 의 측정기로 잰 값):

| 지표 | 범위 | 의미 |
|---|---|---|
| 외곽선 밝기 | 0.03 ~ 0.25 | 유닛마다 외곽선 색 깊이가 다르다 (warrior 0.25, healer 0.19 는 밝은 편) |
| 색 수 | 16 ~ 56 | 같은 "중간 디테일"인데 3배 차이 |
| 캔버스 점유율 | 0.27 ~ 0.49 | guardian, imp 가 높음 |
| 키 비율 | 0.88 ~ 0.97 | 비교적 일정 |

**이 8종을 그대로 앵커로 쓰면 지문의 허용 범위가 넓어진다** (PROCESS 3장 4항: 지문은 앵커보다 일관될 수 없다).

대표 색 (장면 유형별 주요 6색 중 의미 있는 것. **무드 참고용이며 팔레트가 아니다**):

| 장면 | 색 |
|---|---|
| PoE 야외, 바위 | `#242427` `#2f3132` `#3e423c` `#565a4b` `#848974` |
| PoE 동굴, 던전 | `#050504` `#15110e` `#1b1d19` `#343027` `#2d201a` `#515745` |
| PoE 야간 마을 (푸른 그늘) | `#0b1019` `#182734` `#39434f` `#495c6b` `#6c7f8c` |
| Divinity 목조 실내 | `#151414` `#281a16` `#47291a` `#533b2f` `#906f49` |
| Divinity 야외 이끼 | `#1b2317` `#2f382e` `#65684f` `#8b9168` `#c1ba8b` |

## 3. 공통 규칙 (유닛, 배경, 오브젝트 모두)

두 게임이 한 화면에서 일관되게 보이는 이유를 우리 현재 아트와 비교해 뽑았다.

**R1. 카메라는 하나다.** 두 게임은 바닥, 건물, 소품, 캐릭터가 전부 같은 피치(내려다보는 각도)로 그려져 있다.
우리 현재 아트는 섞여 있다: `floor`/`wall` 은 탑다운 텍스처, `iso_*` 는 두께 있는 아이소 블록, `chest`/`shrine` 은 정면 3/4, `forest_oak` 는 측면에 가깝다.
이것이 두 게임 대비 가장 큰 차이다. 제안: **모든 새 에셋을 코드의 투영(셀 64x32 다이아몬드, `FIsoView`)에 맞춘 3/4 시점**으로 통일한다. 바닥은 다이아몬드 풋프린트, 오브젝트는 윗면과 정면이 함께 보이는 같은 피치, 유닛은 정면 스프라이트(발 기준점 하단 중앙).
*(사람 확인 필요: `style.md` 는 새 원근 규약을 만들지 말라고 하지만 코드에 아이소 투영이 이미 있다.)*

**R2. 빛은 한 방향 + 국소 광원.** 스크린샷의 광원 방향은 장면마다 달라서 따라 하지 않는다. `style.md` 의 좌상단 고정이 우선이다.
가져올 것은 "빛은 광원 주변에만 있다"는 구조다: 환경 전체를 밝히지 않고, 횃불/크리스탈/불 주변만 작고 밝은 클러스터로 둔다. 발광은 요청에 명시한다 (`style.md`: 구운 빛과 그림자 금지).

**R3. 명도 순서.** 바닥 < 벽/오브젝트 < 유닛 < 광원. 환경 평균 밝기 0.13~0.20 안에 둔다. 바닥은 값 3단 이내로 좁게, 균열만 한 단 더 어둡게.

**R4. 색 구조.** 장면(타일 가족)당 세 덩어리: 지배색 하나(차가운 중성 돌/이끼), 따뜻한 보조색 하나(목재/흙/횃불), 발광 포인트 하나(청록 크리스탈 또는 주황 불). 유닛은 어두운 베이스 + 클래스 포인트색 하나.

**R5. 실루엣 우선.** 읽히는 것은 세부가 아니라 실루엣이다. 클래스나 몬스터마다 몸보다 큰 특징 1~2개(견갑, 후드, 지팡이/무기 길이)로 구분한다.

**R6. 재질 처리 (픽셀 규칙).** 금속 3단 + 좌상단 가장자리에 1~2px 하이라이트, 천 2~3단, 석재 균열은 1px 어두운 선, 이끼는 바위의 **윗면과 광원 쪽 면**에 군집으로 쌓는다 (poe_07). 반사광, 광택 하이라이트, 그라데이션 금지.

## 4. 유닛

| 참고 | 관찰 | 우리 유닛에 쓰는 규칙 |
|---|---|---|
| dos2_00 | 판금 전신 갑옷: 큰 견갑 + 머리 장식이 실루엣을 만든다. 은색 + 적색 두 덩어리 | warrior: 큰 견갑, 큰 방패, 적색 포인트 하나 |
| dos2_06 | 몸보다 훨씬 긴 지팡이, 견갑에 얹힌 장식, 너덜한 망토 | mage: 지팡이를 머리의 1.5배 이상으로 |
| dos2_09 | 후드와 망토가 어두운 한 덩어리, 활과 화살통이 비대칭 | archer: 어두운 후드 덩어리 + 비대칭 장비 |
| dos1_00 | 로브가 세로 기둥을 이루고 포인트색이 어두운 바닥과 분리됨 | healer, mage: 로브 = 단순한 세로 덩어리 |
| poe_05 | 거인: 세부보다 큰 몸통과 과장된 곤봉 | guardian: 캔버스를 가득 채우고(점유율 0.5 이상 유지) 무기를 과장 |
| poe_03, poe_11 | 거미와 늑대: 낮고 넓은 실루엣 | 낮은 몬스터는 가로로 넓게, 사람형과 실루엣을 구분 |

- 얼굴: 사람 파티원은 후드/바이저/모자챙의 그늘로 가린다. 몬스터는 얼굴 가능.
- 무기와 장비는 읽히도록 약간 과장한다 (64px 에서는 현실 비율이 안 읽힌다).
- 우리 현재 guardian 은 녹색 기계식 갑옷에 주황 코어로 보여서 "숲 폐허" 테마보다 SF 쪽에 가깝다 (눈으로 본 관찰). 새로 만들 때 이끼 낀 돌/식물 소재로 바꾸는 것을 제안한다.

## 5. 배경 (바닥, 벽, 지형)

| 가족 | 참고 | 규칙 | 대체할 현재 파일 |
|---|---|---|---|
| 판석 바닥 | poe_08, dos1_02, dos1_05 | 불규칙 판석, 이음새에 이끼, 1px 균열. 3×3 이어 붙여 이음새 확인 | floor, floor_b, floor_c, iso_floor_* |
| 방사형 자갈 | poe_02, poe_09 | 원형 문양의 중심 장식용 타일 (제단 바닥). 선택 | (신규) |
| 흙, 잔돌, 뼈 | poe_05, poe_10 | 압축된 흙에 작은 돌과 뼈 조각, 명암 차 작게 | forest_ground |
| 풀, 이끼 지면 | poe_07, poe_11, dos1_02 | 올리브/황록 덤불과 풀 경계, 이끼는 R6 | forest_ground |
| 벽, 절벽 | poe_07, poe_06, poe_11 | 이끼 낀 바위 절벽, 석조 흉벽, 원형 돌담. 우리 테마에서는 뿌리 + 이끼 바위 | wall, wall_b |
| 배경막 | dos2_03, poe_02 | 빽빽한 숲 + 발광 녹색 한 점, 가장자리는 거의 검정으로 | backdrop |

아이소 바닥은 현재 `iso_floor_*` 가 윗면 다이아몬드 + 측면 두께(불투명 영역 y 9~54)다. 두께를 유지할지 결정이 필요하다 (§7).

## 6. 오브젝트

| 오브젝트 | 참고 | 가져올 것 | 현재 파일 |
|---|---|---|---|
| 횃불, 화로 (석주 위) | poe_08, 09, 10 | 불꽃 = 화면에서 가장 밝은 2~3색 클러스터, 석주 머리 장식 | (신규) |
| 크리스탈 군집 | poe_08, 09, 10 | 면이 나뉜 3단 명암 + 청록 발광 한 색 | forest_waystone, shrine |
| 제단, 신전 | dos2_04, poe_04 | 아치 + 원형 단 + 발광 룬 석판 무늬 | shrine, iso_trap |
| 계단, 아치 | poe_08, dos2_04 | 단마다 윗면/정면 두 명도 | stairs |
| 상자, 통, 야영 | poe_10, dos2_07, dos2_11 | 2톤 목재 + 철띠, 모닥불과 침낭 | chest, (신규 crate, barrel, campfire) |
| 바위, 덤불, 나무 | poe_07, poe_06, dos2_04 | 이끼는 윗면에, 덤불은 올리브/황록, 나무는 비틀리고 어둡게 | iso_boulders, iso_tree, forest_oak |
| 뼈 더미 | poe_05 | 거인 둥지 장식 | (신규) |

현재 `forest_oak` 와 `chest` 는 눈으로 볼 때 세트에서 가장 밝고 채도가 높다. 새로 만들 때 R3, R4 에 맞춰 낮추는 것을 제안한다.

## 7. 1차 작업 순서와 초안

PROCESS 의 순서를 따른다: **유닛 마스터 → 타일 → 맵 오브젝트** (오브젝트는 승인된 타일/맵 이미지를 배경으로 스타일 매칭해서 만든다).
아래 프롬프트는 스타일 바이블의 고정 문구 뒤에 붙을 `prompt_asset` 초안이다. 스펙으로 확정하기 전에 사람이 본다 (`/make-asset` 3단계).
공통: 64x64, 투명 배경, 정면, 전신, 중앙, 발 기준선 고정, 1px 어두운 단색 외곽선, 좌상단 광원.

| id | kind | `prompt_asset` 초안 |
|---|---|---|
| warrior_master | character_sprite | human warrior, faceless: closed visor helm with a dark eye slit, oversized plate pauldrons, large shield on the left arm, short sword, dark steel with bronze trim and one red tabard accent |
| archer_master | character_sprite | human archer, faceless: deep hood shadowing the face, short cloak, dark olive and brown leathers, quiver over the shoulder, longbow held to the side, asymmetric silhouette |
| mage_master | character_sprite | human mage, faceless: wide-brimmed pointed hat shadowing the face, simple long robe in deep blue-violet with a bronze belt, tall staff at least 1.5x head height with one small glowing orb |
| healer_master | character_sprite | human healer, faceless: soft hood, pale robe with a bronze sash, small glowing green-white focus in one hand, satchel, simple vertical silhouette |
| guardian_master | character_sprite | towering forest-ruin guardian boss, moss-covered stone plates and rock pauldrons, oversized maul, glowing amber slits for eyes, fills most of the canvas, no sci-fi machinery |
| imp_master | character_sprite | small hunched horned imp, wiry limbs, big pointed ears, ember-orange eyes, tattered loincloth, wide low silhouette |
| imp_archer_master | character_sprite | imp archer, same imp anatomy, crude short bow and a quiver, wide low silhouette |
| imp_shaman_master | character_sprite | imp shaman, same imp anatomy, hooded tattered robe, bone staff with one small green flame |

이 8개가 승인되면 타일 가족(§5 표), 이어서 맵 오브젝트(§6 표)로 간다. 타일과 오브젝트의 스펙은 그때 작성한다.

## 8. 막혀 있는 것과 결정할 것

1. **생성 도구**: PixelLab/Layer MCP 도구와 토큰은 이 문서를 만든 세션에 없었다. 생성은 사용자 환경에서 한다: `cd asset-pipeline`, `PIXELLAB_API_TOKEN` 설정, Claude Code 실행, `/make-asset warrior_master character_sprite <§7 초안>`.
2. **Phase 0 (사람)**: 바이블 초안 검토, 팔레트, 앵커. `python scripts/phase0_check.py` 로 진행을 본다.
3. **앵커를 어디서 구하나**:
   - (A) 기존 `assets/art` 에서 일관된 5~6장만 선별. 무료이고 바로 시작할 수 있지만 위 측정처럼 현재 세트가 들쭉날쭉해서 고르는 기준이 필요하고, 새 룩으로 나아가기보다 현재 룩을 잠근다.
   - (B) 탐색 라운드: 이 브리프의 프롬프트로 종류별 후보를 넉넉히 뽑고, 사람이 골라 앵커로 삼는다. "새롭게"에 맞지만 유료 생성이며 PROCESS 에 없는 절차(앵커 부트스트랩)를 추가해야 한다.
4. **R1 카메라 규칙** 승인 여부.
5. **아이소 바닥**: 현재의 두께 있는 블록을 유지할지, 평면 다이아몬드로 바꿀지.
