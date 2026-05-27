# Development Workflow

## 목표

이 저장소는 Git을 메인 동기화 수단으로 사용한다. Linux/macOS에서는 주로
코드를 작성하고, Windows 별도 checkout에서 native MSVC 빌드, 실행, 디버깅을
수행한다.

깊은 디버깅은 Windows에서 Visual Studio, PIX, RenderDoc 같은 native 도구로
진행한다. SSH는 Windows 머신에 접속해 `git pull`, configure, build, test를
실행하는 리모컨 용도로만 사용한다.

## 현재 프로젝트 구조

- 빌드 시스템: CMake
- 의존성 흐름: vcpkg manifest mode (`vcpkg.json`, `vcpkg-configuration.json`)
- 실행 타깃: `game01P`
- 소스 루트: `src/`
- 주요 의존성: SDL3, EnTT, glm, SDL3_image, SDL3_ttf, ImGui, Tracy, spdlog,
  miniaudio

## CMake Presets

공통 출력 경로는 `out/build/<preset>`이다. `VCPKG_ROOT`는 vcpkg checkout을
가리켜야 한다.

Windows:

```powershell
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug

cmake --preset windows-msvc-release
cmake --build --preset windows-msvc-release
ctest --preset windows-msvc-release
```

Linux:

```bash
cmake --preset linux-clang-debug
cmake --build --preset linux-clang-debug
ctest --preset linux-clang-debug

cmake --preset linux-clang-release
cmake --build --preset linux-clang-release
ctest --preset linux-clang-release
```

macOS:

```bash
cmake --preset macos-clang-debug
cmake --build --preset macos-clang-debug
ctest --preset macos-clang-debug
```

macOS preset은 host가 Darwin일 때만 노출된다. vcpkg triplet은 명시하지 않으므로
필요하면 `VCPKG_DEFAULT_TRIPLET`로 지정한다.

## Windows 원격 빌드

macOS/Linux/WSL에서 Windows SSH 서버로 명령을 보내려면:

```bash
export GAME01P_WIN_HOST="your-windows-host"
export GAME01P_WIN_USER="your-windows-user"
export GAME01P_WIN_REPO="C:/dev/game01P"
export GAME01P_WIN_CONFIGURE_PRESET="windows-msvc-debug"
export GAME01P_WIN_BUILD_PRESET="windows-msvc-debug"
export GAME01P_WIN_TEST_PRESET="windows-msvc-debug"

./scripts/remote-win-build.sh
```

선택 변수:

- `GAME01P_WIN_PORT`: SSH 포트, 기본값 `22`
- `GAME01P_WIN_BRANCH`: 지정 시 `git fetch` 후 해당 branch checkout
- `GAME01P_REMOTE_PULL`: `1`이면 `git pull --ff-only` 실행, 기본값 `1`
- `GAME01P_REMOTE_TEST`: `1`이면 `ctest --preset` 실행, 기본값 `1`

기존 호환 스크립트 `scripts/remote-windows-check.sh`와
`scripts/check-windows.ps1`도 유지된다. 새 스크립트는 preset 기반이고,
기존 스크립트는 `-Configuration Debug|Release` 중심이다.

## 작업 규칙

```text
1. Linux/macOS에서 코드 작성과 diff 리뷰를 한다.
2. Git commit 또는 push로 Windows checkout과 동기화한다.
3. 필요할 때 SSH로 Windows에서 pull/build/test를 실행한다.
4. 실행, 그래픽, GPU, native 디버깅은 Windows에서 직접 수행한다.
```

네트워크 공유 폴더에서 직접 빌드하지 않는다. 각 OS마다 일반 checkout을 유지하고
Git으로만 동기화한다.
