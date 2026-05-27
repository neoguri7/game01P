#!/usr/bin/env bash
set -euo pipefail

# Edit these defaults or override them with environment variables.
GAME01P_WIN_HOST="${GAME01P_WIN_HOST:-}"
GAME01P_WIN_USER="${GAME01P_WIN_USER:-}"
GAME01P_WIN_PORT="${GAME01P_WIN_PORT:-22}"
GAME01P_WIN_REPO="${GAME01P_WIN_REPO:-C:/dev/game01P}"
GAME01P_WIN_BRANCH="${GAME01P_WIN_BRANCH:-}"
GAME01P_WIN_CONFIGURE_PRESET="${GAME01P_WIN_CONFIGURE_PRESET:-windows-msvc-debug}"
GAME01P_WIN_BUILD_PRESET="${GAME01P_WIN_BUILD_PRESET:-windows-msvc-debug}"
GAME01P_WIN_TEST_PRESET="${GAME01P_WIN_TEST_PRESET:-windows-msvc-debug}"
GAME01P_REMOTE_PULL="${GAME01P_REMOTE_PULL:-1}"
GAME01P_REMOTE_TEST="${GAME01P_REMOTE_TEST:-1}"

if [[ -z "$GAME01P_WIN_HOST" ]]; then
  echo "error: set GAME01P_WIN_HOST to the Windows SSH host." >&2
  exit 1
fi

ssh_target="$GAME01P_WIN_HOST"
if [[ -n "$GAME01P_WIN_USER" ]]; then
  ssh_target="$GAME01P_WIN_USER@$GAME01P_WIN_HOST"
fi

ssh_options=(-p "$GAME01P_WIN_PORT")

remote_commands="\$ErrorActionPreference = 'Stop';"
remote_commands+=" function Invoke-Native { param([string]\$FilePath, [string[]]\$Arguments = @()); & \$FilePath @Arguments; if (\$LASTEXITCODE -ne 0) { throw \"\$FilePath failed with exit code \$LASTEXITCODE\" } };"
remote_commands+=" Set-Location '$GAME01P_WIN_REPO';"

if [[ -n "$GAME01P_WIN_BRANCH" ]]; then
  remote_commands+=" Invoke-Native 'git' @('fetch', '--all', '--prune');"
  remote_commands+=" Invoke-Native 'git' @('checkout', '$GAME01P_WIN_BRANCH');"
fi

if [[ "$GAME01P_REMOTE_PULL" == "1" ]]; then
  remote_commands+=" Invoke-Native 'git' @('pull', '--ff-only');"
fi

remote_commands+=" Invoke-Native 'cmake' @('--preset', '$GAME01P_WIN_CONFIGURE_PRESET');"
remote_commands+=" Invoke-Native 'cmake' @('--build', '--preset', '$GAME01P_WIN_BUILD_PRESET');"

if [[ "$GAME01P_REMOTE_TEST" == "1" && -n "$GAME01P_WIN_TEST_PRESET" ]]; then
  remote_commands+=" Invoke-Native 'ctest' @('--preset', '$GAME01P_WIN_TEST_PRESET');"
fi

ssh "${ssh_options[@]}" "$ssh_target" "powershell -NoProfile -ExecutionPolicy Bypass -Command \"$remote_commands\""
