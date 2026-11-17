#!/bin/bash
# macOS (Apple Silicon) package: out/package/EmberDungeon.app with the assets inside the bundle, plus a zip of it.
# Usage: bash package/macos/package.sh     (no arguments; run from anywhere inside the checkout)
# Needs: Xcode command line tools (xcode-select --install), cmake and ninja (brew install cmake ninja pkg-config).
set -euo pipefail
fail() { local code="$1"; shift; echo "PACKAGE FAIL: $*" >&2; exit "$code"; }
[[ $# -eq 0 ]] || fail 64 "package.sh takes no arguments"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"
[[ "$(uname -s)" == "Darwin" && "$(uname -m)" == "arm64" ]] || fail 1 "needs macOS on Apple Silicon (found $(uname -s) $(uname -m))"
for tool in git cmake ninja clang++ codesign ditto otool; do
  command -v "$tool" >/dev/null 2>&1 || fail 1 "missing '$tool' (xcode-select --install; brew install cmake ninja pkg-config)"
done
export MACOSX_DEPLOYMENT_TARGET=13.3
if [[ -z "${VCPKG_ROOT:-}" ]]; then
  export VCPKG_ROOT="$ROOT/out/vcpkg"
  if [[ ! -x "$VCPKG_ROOT/vcpkg" ]]; then
    rm -rf "$VCPKG_ROOT"
    git clone --quiet https://github.com/microsoft/vcpkg "$VCPKG_ROOT" || fail 1 "cannot clone vcpkg"
    "$VCPKG_ROOT/bootstrap-vcpkg.sh" -disableMetrics || fail 1 "cannot bootstrap vcpkg"
  fi
fi

PRESET=macos-clang-package
BUILD="$ROOT/out/build/$PRESET"
APP="$BUILD/EmberDungeon.app"
EXE="$APP/Contents/MacOS/EmberDungeon"
rm -rf "$BUILD/package-gen" "$APP"
cmake --preset "$PRESET" || fail 1 "configure failed"
cmake --build --preset "$PRESET" --target game01P || fail 2 "build failed"
[[ -x "$EXE" ]] || fail 3 "missing $EXE"
codesign --verify --deep --strict "$APP" || fail 5 "the bundle's code signature does not verify"
DEPS="$(otool -L "$EXE" | tail -n +2 | awk '{print $1}')"
BAD="$(echo "$DEPS" | grep -vE '^(/usr/lib/|/System/Library/)' || true)"
[[ -z "$BAD" ]] || fail 5 "non-system libraries: $BAD"

CHECK="$ROOT/out/package-check"
rm -rf "$CHECK"
mkdir -p "$CHECK/cwd"
SMOKE_JSON=""
smoke() {  # smoke <name> [VAR=value ...]: windowless run from an empty working directory; records the exit code
  local name="$1"; shift
  local rc=0
  ( cd "$CHECK/cwd" && env SDL_VIDEO_DRIVER=dummy SDL_AUDIO_DRIVER=dummy SDL_RENDER_DRIVER=software \
      GAME01P_SAVE_DIR="$CHECK/save-$name" GAME01P_PACKAGE_CACHE="$CHECK/cache" "$@" \
      "$EXE" --ember --smoke-test > "$CHECK/$name.log" 2>&1 ) || rc=$?
  [[ $rc -eq 0 ]] || { tail -n 30 "$CHECK/$name.log" >&2; fail 5 "smoke '$name' failed (exit $rc)"; }
  SMOKE_JSON="${SMOKE_JSON:+$SMOKE_JSON,}\"$name\":$rc"
}
smoke title
smoke battle GAME01P_START=battle
CACHE_DIRS=("$CHECK"/cache/*/)
[[ ${#CACHE_DIRS[@]} -eq 1 && -f "${CACHE_DIRS[0]}assets/data/units.json" ]] \
  || fail 5 "expected one cache directory with assets/data/units.json under $CHECK/cache"
[[ -f "${CACHE_DIRS[0]}game01p.log" ]] || fail 5 "no game01p.log in the cache directory"
[[ -z "$(ls -A "$CHECK/cwd")" ]] || fail 5 "the packaged game wrote into its working directory: $(ls -A "$CHECK/cwd")"
count() { find "$1" -type f | wc -l | tr -d ' '; }
SOURCE_FILES="$(count "$ROOT/assets")"
[[ "$SOURCE_FILES" == "$(count "$APP/Contents/Resources/assets")" && "$SOURCE_FILES" == "$(count "${CACHE_DIRS[0]}assets")" ]] \
  || fail 5 "asset count mismatch (source $SOURCE_FILES)"

OUT="$ROOT/out/package"
rm -rf "$OUT"
mkdir -p "$OUT"
ditto "$APP" "$OUT/EmberDungeon.app"
codesign --verify --deep --strict "$OUT/EmberDungeon.app" || fail 5 "the staged bundle's code signature does not verify"
ditto -c -k --keepParent "$OUT/EmberDungeon.app" "$OUT/EmberDungeon-macos-arm64.zip"
TREE="$(git rev-parse 'HEAD^{tree}' 2>/dev/null || true)"
MINOS="$(otool -l "$EXE" | awk '/LC_BUILD_VERSION/{f=1} f && $1=="minos" && !done {print $2; done=1}')"
[[ -n "$MINOS" ]] || fail 5 "cannot read the minimum macOS version of $EXE"
DEPS_JSON="$(echo "$DEPS" | awk 'BEGIN{printf "["} NR>1{printf ","} {printf "\"%s\"", $0} END{printf "]"}')"
ZIP_BYTES="$(stat -f %z "$OUT/EmberDungeon-macos-arm64.zip")"
cat > "$OUT/macos-package-evidence.json" <<EOF
{"tree":"$TREE","arch":"arm64","minos":"$MINOS","assetFiles":$SOURCE_FILES,"smoke":{$SMOKE_JSON},"deps":$DEPS_JSON,"zipBytes":$ZIP_BYTES}
EOF
echo "PACKAGE OK out/package/EmberDungeon.app and out/package/EmberDungeon-macos-arm64.zip"
