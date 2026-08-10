# game01P

## 개발 워크플로우

macOS/Linux는 코드 작성과 가벼운 점검에 사용하고, native Windows를 빌드와 실행 검증의 기준 환경으로 사용합니다.
`main`은 항상 빌드 가능한 기준선으로 유지하고, 작업은 짧은 feature/fix/chore 브랜치에서 진행합니다.

자세한 내용은 [DEV_WORKFLOW.md](DEV_WORKFLOW.md)를 참고하세요.

## Native Linux 빌드

Linux에서 `linux-clang-*` 프리셋으로 configure + build + ctest를 한 번에
수행합니다 (Clang/Ninja 필요).

```bash
bash scripts/check-linux.sh Debug      # configure+build+ctest
```

Windows 원격 빌드 리모컨:

```bash
GAME01P_WIN_HOST="your-windows-host" ./scripts/remote-windows-check.sh
GAME01P_WIN_BRANCH="feature/input-actions" GAME01P_WIN_HOST="your-windows-host" ./scripts/remote-windows-check.sh
```

## Checkout 동기화 규칙

macOS/Linux checkout과 Windows checkout은 서로 다른 작업 폴더입니다. 한쪽에서
작업한 뒤 다른 쪽으로 넘어가기 전에 Git으로 동기화하세요.

```text
작업 전: git pull --ff-only
작업 후: commit + push
다른 머신으로 이동: git pull --ff-only
```

동시에 양쪽 checkout에서 같은 파일을 수정하지 마세요. Windows Remote SSH는 주로
작업 브랜치의 `git pull`, configure, build, test, 로그 확인 용도로 사용합니다.

## Windows 빌드 다운로드 방법

1. GitHub 저장소의 Actions 탭을 엽니다.
2. 최신 성공한 "Windows Build" 실행을 선택합니다.
3. 실행 요약에서 `game01P-windows-Release` artifact를 다운로드합니다.
