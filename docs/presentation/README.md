# 중간 발표자료 (2026-09-17)

`mid-presentation.html` — 단일 파일 슬라이드 15장. 브라우저로 열면 바로 동작하고, **인터넷 불필요**
(mermaid 11.4.1을 `vendor/mermaid.min.js`로 vendoring). `Ctrl+P` → PDF 저장 가능(print stylesheet 포함).

- 이동: `←` `→` `Space` `Home` `End` · URL 해시(`#s7`)로 슬라이드 직행
- 내용: 엔진 요약 1장 · 구현된 게임 메커니즘 6장(슬라이스 1–3) · 방법론/검증 2장 · 남은 것 2장 · 정리 1장
- 시각화 13개: flowchart 8 · sequenceDiagram 1 · stateDiagram 1 · xychart 1 (외 2 flowchart가 엔진/파이프라인)

## 편집 규칙 (레이아웃이 깨지는 이유를 없애기 위한 계약)

1. **슬라이드 골격은 항상 동일** — `.head`(제목+한 줄 부제) → `.body` → `.foot`(각주/태그).
   `.body`는 `split`(다이어그램 1.45fr | 노트 1fr) · `split wide`(1.8fr) · `three`(3패널) · `full` 중 하나.
2. **다이어그램 방향** — 노드 깊이(rank) **≤ 4는 `flowchart LR`, ≥ 5는 `flowchart TB`**.
   5개 이상을 LR로 두면 폭이 화면을 넘고 SVG가 축소돼 글자가 뭉개진다.
3. **라벨은 8자 이내**, 줄바꿈은 `<br/>` 한 번까지. 긴 라벨 = 넓은 박스 = 레이아웃 붕괴.
4. **슬라이드당 다이어그램 1개.** 같은 얘기를 하는 두 그림은 합친다(엔진 계층 + ECS 규칙 → 1장).
5. `stateDiagram`의 `note`는 쓰지 않는다 — 폭을 키운다. 노트는 옆 패널 텍스트로.
6. 텍스트는 **패널당 불릿 3개 이하, 한 줄 40자 이하.** 부제는 한 줄.
7. 다이어그램 크기는 CSS가 정한다(`.diagram svg{max-height:min(54vh,470px)}`) — 다이어그램 내부에서
   크기 조정하지 않는다.

## 검증

```bash
mkdir -p /tmp/mmd && cd /tmp/mmd && npm install jsdom   # jsdom은 저장소 의존성이 아니다
NODE_PATH=/tmp/mmd/node_modules node docs/presentation/verify.js
```

`verify.js`는 브라우저 없이 vendored mermaid로 **13개 다이어그램을 전부 실제 파싱**하고,
슬라이드/다이어그램 수 · 태그 균형(스택 스캔) · 오프라인 자산 · 키보드 라우팅 · 인쇄 스타일을 확인한다.
현재: `slides 15 · diagrams 13 (all parse) · tag balance ok · exit 0`.
