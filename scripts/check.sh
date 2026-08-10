#!/usr/bin/env bash
# Configure, build and test the project on the current host (macOS, or Linux/WSL)
# using the platform's canonical preset (macos-clang-* / linux-clang-*).
# On Windows use scripts/check-windows.ps1 instead.
set -euo pipefail

configuration="${1:-Debug}"
case "$configuration" in
  Debug|Release) ;;
  *)
    echo "usage: $0 [Debug|Release]" >&2
    exit 2
    ;;
esac

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
configuration_lower="$(printf '%s' "$configuration" | tr '[:upper:]' '[:lower:]')"

case "$(uname -s)" in
  Darwin) preset="macos-clang-${configuration_lower}" ;;
  Linux)  preset="linux-clang-${configuration_lower}" ;;
  *)
    echo "error: unsupported host $(uname -s); use check-windows.ps1 on Windows." >&2
    exit 2
    ;;
esac

# Resolve vcpkg: VCPKG_ROOT, then a sibling checkout, then a local one.
if [[ -z "${VCPKG_ROOT:-}" ]]; then
  if [[ -d "$repo_root/../vcpkg" ]]; then
    export VCPKG_ROOT="$(cd "$repo_root/../vcpkg" && pwd)"
  elif [[ -d "$repo_root/vcpkg" ]]; then
    export VCPKG_ROOT="$repo_root/vcpkg"
  else
    echo "error: VCPKG_ROOT is not set and no sibling/local vcpkg checkout was found." >&2
    exit 1
  fi
fi

toolchain_file="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
if [[ ! -f "$toolchain_file" ]]; then
  echo "error: vcpkg toolchain file not found: $toolchain_file" >&2
  exit 1
fi

cmake --preset "$preset"
cmake --build --preset "$preset" --parallel
ctest --preset "$preset"
