# 중간 발표자료 (2026-09-17)

`mid-presentation.html` — 단일 파일 슬라이드 17장. 브라우저로 열면 바로 동작하고, **인터넷 불필요**
(mermaid 11.4.1을 `vendor/mermaid.min.js`로 vendoring). `Ctrl+P` → PDF 저장 가능(print stylesheet 포함).

## 발표 3축 (슬라이드 배치)

| 축 | 슬라이드 | 내용 |
| --- | --- | --- |
| 1. 구현된 엔진 구조 | 2–6 | 계층 지도 · 한 프레임 · 배치 계약(표) · 던전 입장 경로 · 전투 1턴 경로 |
| 2. 기획 + 검증한 매커니즘 | 7–13 | 문서 구조 · 게임 루프 · 런 초기화/누적(표) · 전투 시스템 · 행동 트리 · 실제 로그 문장 · 데이터/결정론 |
| 3. 앞으로 (큰 틀) | 14–16 | 로드맵 4덩어리 · 미결→결정 순서(표) · 작업 규범(R1–R33, 3패스 리뷰) |

1 = 표지, 17 = 정리. 시각화 12개: flowchart 9 (LR 5 · TB 4) · sequenceDiagram 3.

## 편집 규칙 (레이아웃이 깨지는 이유를 없애기 위한 계약)

1. **슬라이드 골격은 항상 동일** — `.head`(제목+한 줄 부제) → `.body` → `.foot`(각주/태그).
   `.body`는 `split`(다이어그램 1.45fr | 노트 1fr) · `split wide`(1.8fr) · `three`(3패널) · `full` 중 하나.
2. **다이어그램 방향** — 노드 깊이(rank) **≤ 4는 `flowchart LR`, ≥ 5는 `flowchart TB`**.
   5개 이상을 LR로 두면 폭이 화면을 넘고 SVG가 축소돼 글자가 뭉개진다.
3. **라벨은 8자 이내**, 줄바꿈은 `<br/>` 한 번까지. 긴 라벨 = 넓은 박스 = 레이아웃 붕괴.
4. **슬라이드당 다이어그램 1개.** 같은 얘기를 하는 두 그림은 합친다.
5. `stateDiagram`의 `note`는 쓰지 않는다 — 폭을 키운다. 노트는 옆 패널 텍스트로.
6. 텍스트는 **패널당 불릿 3~5개, 한 줄 40자 이하.** 부제는 한 줄.
7. 다이어그램 크기는 CSS가 정한다(`.diagram svg{max-height:min(54vh,470px)}`) — 다이어그램 내부에서
   크기 조정하지 않는다.
8. **mermaid 블록 안의 `>`는 `&gt;`로 쓴다** (`A --&gt; B`). HTML 파서가 텍스트 노드에서 디코드하므로
   mermaid는 `-->`를 그대로 받는다 — 소스에 raw `>`를 두면 린트가 "Special characters must be escaped"로
   막는다. `verify.js`는 같은 규칙으로 디코드한 뒤 파싱한다. `<br/>`도 같은 이유로 `&lt;br/&gt;`.
9. 다이어그램 HTML 라벨(`"문자열"`) 안에 `(`, `"`를 쓰지 않는다 — 파서가 흔들린다. 괄호 설명은 패널로.

## 검증

```bash
mkdir -p /tmp/mmd && cd /tmp/mmd && npm install jsdom   # jsdom은 저장소 의존성이 아니다
NODE_PATH=/tmp/mmd/node_modules node docs/presentation/verify.js
```

`verify.js`는 브라우저 없이 vendored mermaid로 **12개 다이어그램을 전부 실제 파싱**하고,
슬라이드/다이어그램 수 · 태그 균형(스택 스캔) · 오프라인 자산 · 키보드 라우팅 · 인쇄 스타일 · 이스케이프된
화살표 수를 확인한다. 현재: `slides 17 · diagrams 12 (all parse) · tag balance ok · exit 0`.

## 배포 위치

- 저장소 원본: `docs/presentation/` (branch `docs/mid-presentation`) — git 이력이 곧 버전 관리.
- 발표용 사본: `D:\repos\obs-main\presentation\` (Obsidian vault, git 아님) — 이 폴더의
  `mid-presentation.html` + `vendor/` + `verify.js`를 저장소 원본에서 복사해 둔다.
  직접 편집은 저장소 쪽에서 하고 복사하는 편이 안전하다.
