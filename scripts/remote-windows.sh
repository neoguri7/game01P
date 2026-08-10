#!/usr/bin/env bash
# SSH into a native Windows build machine, sync the repo, and run a Windows
# build (and run the binary if requested). Configuration comes from
# scripts/remote-windows.env (or GAME01P_* environment variables).
set -euo pipefail

config_file="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/remote-windows.env"
if [[ -f "$config_file" ]]; then
  # shellcheck disable=SC1090
  source "$config_file"
fi

: "${GAME01P_WIN_HOST:?Set GAME01P_WIN_HOST to the Windows PC SSH host.}"

GAME01P_WIN_USER="${GAME01P_WIN_USER:-}"
GAME01P_WIN_PORT="${GAME01P_WIN_PORT:-22}"
GAME01P_WIN_REPO="${GAME01P_WIN_REPO:-C:/dev/game01P}"
GAME01P_WIN_CONFIG="${GAME01P_WIN_CONFIG:-Debug}"
GAME01P_CMAKE_GENERATOR="${GAME01P_CMAKE_GENERATOR:-}"
GAME01P_WIN_BRANCH="${GAME01P_WIN_BRANCH:-}"
GAME01P_REMOTE_PULL="${GAME01P_REMOTE_PULL:-1}"
GAME01P_REMOTE_SLEEP="${GAME01P_REMOTE_SLEEP:-0}"
GAME01P_SSH_WAIT_SECONDS="${GAME01P_SSH_WAIT_SECONDS:-120}"

ssh_target="$GAME01P_WIN_HOST"
if [[ -n "$GAME01P_WIN_USER" ]]; then
  ssh_target="$GAME01P_WIN_USER@$GAME01P_WIN_HOST"
fi
ssh_options=(-p "$GAME01P_WIN_PORT")

# Optional Wake-on-LAN to power on a sleeping build box.
if [[ -n "${GAME01P_WOL_MAC:-}" ]]; then
  if command -v wakeonlan >/dev/null 2>&1; then
    wakeonlan "$GAME01P_WOL_MAC" >/dev/null
  elif command -v wol >/dev/null 2>&1; then
    wol "$GAME01P_WOL_MAC" >/dev/null
  else
    echo "warning: GAME01P_WOL_MAC is set, but neither wakeonlan nor wol is installed." >&2
  fi
fi

# Wait until SSH is reachable (useful right after a wake).
deadline=$((SECONDS + GAME01P_SSH_WAIT_SECONDS))
until ssh "${ssh_options[@]}" -o BatchMode=yes -o ConnectTimeout=5 "$ssh_target" "echo ready" >/dev/null 2>&1; do
  if (( SECONDS >= deadline )); then
    echo "error: SSH did not become ready within ${GAME01P_SSH_WAIT_SECONDS}s: $ssh_target" >&2
    exit 1
  fi
  sleep 5
done

remote_commands="\$ErrorActionPreference = 'Stop';"
remote_commands+=" function Invoke-Native { param([string]\$FilePath, [string[]]\$Arguments = @()); & \$FilePath @Arguments; if (\$LASTEXITCODE -ne 0) { throw \"\$FilePath failed with exit code \$LASTEXITCODE\" } };"
remote_commands+=" Set-Location '$GAME01P_WIN_REPO';"

if [[ -n "$GAME01P_WIN_BRANCH" ]]; then
  remote_commands+=" Invoke-Native 'git' @('fetch', '--all', '--prune'); Invoke-Native 'git' @('checkout', '$GAME01P_WIN_BRANCH');"
fi

if [[ "$GAME01P_REMOTE_PULL" == "1" ]]; then
  remote_commands+=" Invoke-Native 'git' @('pull', '--ff-only');"
fi

# Run the native Windows build/test entry point.
if [[ -n "$GAME01P_CMAKE_GENERATOR" ]]; then
  remote_commands+=" & .\\scripts\\check-windows.ps1 -Configuration '$GAME01P_WIN_CONFIG' -Generator '$GAME01P_CMAKE_GENERATOR';"
else
  remote_commands+=" & .\\scripts\\check-windows.ps1 -Configuration '$GAME01P_WIN_CONFIG';"
fi

if [[ "$GAME01P_REMOTE_SLEEP" == "1" ]]; then
  remote_commands+=" rundll32.exe powrprof.dll,SetSuspendState 0,1,0;"
fi

ssh "${ssh_options[@]}" "$ssh_target" "powershell -NoProfile -ExecutionPolicy Bypass -Command \"$remote_commands\""
