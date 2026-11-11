# 플레이테스트 폴더

한 세션마다 파일 하나: `docs/playtest/YYYY-MM-DD.md`. 시작은 `TEMPLATE.md`를 복사한다.

## 창을 띄워 시작하기
사용자의 실제 세이브(`%APPDATA%\Game01P`)를 건드리지 않도록 세이브 폴더를 따로 준다.

```powershell
node .claude/scripts/build.mjs --preset windows-msvc-agent
$env:GAME01P_SAVE_DIR = "D:\repos\game01P\out\playtest"
.\out\build\windows-msvc-agent\Debug\game01P.exe
```

같은 런을 다시 보려면 `GAME01P_SEED=<숫자>`를 함께 준다.

## 런 로그
- 런마다 JSONL 파일이 생긴다: `<세이브 폴더>/run_logs/run-<시드>-<번호>.jsonl`.
- 플레이어 화면에는 아무것도 보이지 않는다.

## 숫자 보기
```powershell
node .claude/scripts/runlog-report.mjs out\playtest\run_logs
```
- 폴더 안의 모든 `*.jsonl`을 합쳐 전투·스킬·룬·갈림길·이벤트·캠프·빗나감 표를 찍는다. `--json`을 붙이면 원자료.
- 창 없이 여러 시드를 빠르게 돌려 기준선을 잡으려면: `node .claude/scripts/baseline.mjs --label <이름> --seeds 1-5`.

## 메모 규칙
- 메모는 사용자의 말 그대로 적는다. 혼잣말, 멈칫한 곳, 헷갈린 곳도 그대로.
- 에이전트는 메모를 고쳐 쓰지 않는다. WP 가설이나 `docs/design/decisions.md` 항목으로 분류만 한다.
- 두 플레이테스트 사이에 플레이어에게 보이는 WP는 최대 3개(CLAUDE.md).
