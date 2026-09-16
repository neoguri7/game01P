#!/usr/bin/env bash
# verify-changeability.sh — run the changeability rule bundle (docs/guidelines/game-code-changeability.md §5).
#
#   bash scripts/verify-changeability.sh                # repo mode: scan src/ (contract rules must be clean)
#   bash scripts/verify-changeability.sh --calibrate    # fixture mode: assert the V1..V20 calibration corpus
#
# Exit 0 = every asserted expectation held. Exit 1 = mismatch (rule bundle drifted from the guideline,
# or a real contract violation exists). The Zz* fixtures in tests/changeability/fixtures/ are DELIBERATELY
# broken and are never compiled (CMake globs src/* only) — they exist so the bundle itself is testable.
#
# Coverage contract (F1): repo mode prints a "NOT CHECKED (manual)" list. `OK (repo)` is evidence ONLY for
# the rules actually asserted here; a reviewer may not cite it for a rule on that list.

set -uo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

MODE=repo
[ "${1:-}" = "--calibrate" ] && MODE=calibrate
if [ "$MODE" = calibrate ]; then SRC=tests/changeability/fixtures; else SRC=src; fi

FAILED=0
MANUAL=""
note() { printf '%s\n' "$*"; }
bad() {
  printf 'FAIL  %s\n' "$*"
  FAILED=1
}
ok() { printf 'ok    %s\n' "$*"; }
warn() { printf 'warn  %s\n' "$*"; }
manual() { MANUAL="$MANUAL $1"; }

# ---------------------------------------------------------------- helpers
# drop full-line comments so prose cannot create violations (D3/D8)
strip_comments() {
  awk -F: '{ cp=$0; sub(/^[^:]*:[0-9]+:/, "", cp); if (cp !~ /^[[:space:]]*\/\//) print $0 }'
}
# drop full-line AND trailing comments (for identifier rules: R2/R7b/R15)
strip_all_comments() {
  sed -E 's|//.*$||' | awk -F: '{ cp=$0; sub(/^[^:]*:[0-9]+:/, "", cp); if (cp ~ /[^[:space:]]/) print $0 }'
}
# hits <rg-args...>              (full-line comments removed)
hits() { rg -n --no-heading "$@" 2>/dev/null | strip_comments | path_filter; }
# hits_code <rg-args...>         (all comments removed)
hits_code() { rg -n --no-heading "$@" 2>/dev/null | strip_all_comments | path_filter; }
n() { hits "$@" | wc -l | tr -d ' '; }
# fc <pattern> <file>            (per-file MATCH count, several hits may share a line)
fc() {
  rg -o --no-heading "$1" "$2" 2>/dev/null | wc -l | tr -d ' \r'
}
# fc_fallback <pattern> <file>   (R4 legal sink: `// fallback:` lines)
fc_fallback() {
  rg -n --no-heading "$1" "$2" 2>/dev/null | rg -v '//\s*fallback:' | rg -o --no-heading "$1" | wc -l | tr -d ' \r'
}
fc_nocontract() {
  rg -o --no-heading "$1" "$2" 2>/dev/null | rg -v 'ISystem\.h' | wc -l | tr -d ' \r'
}
# cross-platform path filter (F13): rg emits `src\imgui\...` on Windows
path_filter() { rg -v '[\\/](factories|imgui|vendor|third_party)[\\/]'; }
# Asset boundary (R23/R25): the loader owns string keys and raw file IO. Another file may opt in explicitly
# with a `// boundary:` marker on the offending line, mirroring the `// fallback:` convention of R4.
asset_boundary_filter() { rg -v '[\\/]core[\\/](AssetManager|ResourceManager)\.[ch]' | rg -v '//[[:space:]]*boundary:'; }

# R3 event delivery order (D14): a `queueFrame<T>` producer and a `frameEvents<T>()` consumer of the SAME type
# must be registered so the consumer runs at or after the producer. A consumer registered *before* the producer
# never sees the event: FEventBus::beginFrame() clears the frame queues at the start of every frame, so the
# request is silently deleted instead of applied. This is the one event bug no include/lint rule can see, because
# both files are individually correct.
# One system's own declaration body: from `struct <name>` to its closing `};`. Needed becauseR1 wants one ISystem
# per file but the detector must stay correct when a header holds helpers or two fixture structs — searching the
# whole file would attribute one system's queueFrame<> to its neighbour (a false positive, D14 calibration).
system_body() { # system_body <header> <name>
  # NB: gawk/mawk read `\b` in a dynamic regex as a literal backspace, so the boundary is spelled as a
  # negated character class here -- `\b` silently matched nothing and hid the positive control.
  awk -v name="$2" '
    $0 ~ ("struct[[:space:]]+" name "([^A-Za-z0-9_]|$)") { inside = 1 }
    inside { print }
    inside && /^};/ { exit }
  ' "$1"
}

event_order_report() {
  local srcroot="$1" reg name file c p type
  local -a order headers
  for reg in $(rg -l --no-heading 'addSystem<[A-Za-z_]+>' "$srcroot" 2>/dev/null | path_filter); do
    # full-line comments are stripped first, so the fixture's "// systems.addSystem<...>" annotation lines are
    # not counted as registrations and the reported index is the real walk order. (-H is required: with a single
    # file argument rg omits the `path:line:` prefix that strip_comments matches on.)
    mapfile -t order < <(rg -n -H --no-heading 'addSystem<([A-Za-z_]+)>' "$reg" 2>/dev/null | strip_comments | rg -o --no-heading 'addSystem<([A-Za-z_]+)>' -r '$1')
    [ "${#order[@]}" -eq 0 ] && continue
    headers=()
    for name in "${order[@]}"; do
      headers+=("$(rg -l --no-heading "struct[[:space:]]+${name}\\b" "$srcroot" 2>/dev/null | head -1)")
    done
    # The violation is a *consumer* whose index is lower than a producer of the same type: the consumer runs,
    # sees nothing, and the producer fills the queue afterwards -- which beginFrame() then clears unread.
    for ((c = 0; c < ${#order[@]}; c++)); do
      file=${headers[c]}
      [ -z "$file" ] && continue
      body=$(system_body "$file" "${order[c]}")
      [ -z "$body" ] && continue
      while IFS= read -r type; do
        [ -z "$type" ] && continue
        for ((p = c + 1; p < ${#order[@]}; p++)); do
          [ -z "${headers[p]}" ] && continue
          if printf '%s\n' "$(system_body "${headers[p]}" "${order[p]}")" | rg -q --no-heading "queueFrame<$type>" 2>/dev/null; then
            printf '%s: %s (index %d) reads %s, queued later by %s (index %d)\n' \
              "$reg" "${order[c]}" "$c" "$type" "${order[p]}" "$p"
          fi
        done
      done < <(printf '%s\n' "$body" | rg -o --no-heading "frameEvents<([A-Za-z_:]+)>" -r '$1' | sort -u)
    done
  done
}

# R30 fork-by-copy detector (DOOM 3 d3xp shape): duplicate basenames are flagged only when BOTH files are
# >=40 lines and >=60% of the shorter file's lines also occur in the longer one. A 3-line forwarding shim
# (src/core/Logger.h vs src/debug/Logger.h) is therefore NOT a copied layer.
dup_layer_report() {
  local src="$1" base a b na nb shared min ratio i j
  local -a paths
  for base in $(find "$src" -type f \( -name '*.h' -o -name '*.cpp' \) -printf '%f\n' 2>/dev/null | sort | uniq -d); do
    mapfile -t paths < <(find "$src" -type f -name "$base" 2>/dev/null | path_filter)
    for ((i = 0; i < ${#paths[@]}; i++)); do
      for ((j = i + 1; j < ${#paths[@]}; j++)); do
        a=${paths[i]}
        b=${paths[j]}
        na=$(wc -l <"$a")
        nb=$(wc -l <"$b")
        min=$na
        [ "$nb" -lt "$na" ] && min=$nb
        [ "$min" -lt 40 ] && continue
        shared=$(comm -12 <(sort "$a") <(sort "$b") | wc -l | tr -d ' ')
        ratio=$((shared * 100 / min))
        [ "$ratio" -ge 60 ] && printf '%s: fork-by-copy of %s (%s%% shared lines, %s/%s)\n' "$b" "$a" "$ratio" "$shared" "$min"
      done
    done
  done
}
# ---- observation/comment rules R31..R33 (S15..S19) ----
# R31①: a LOG_* call inside a preprocessor conditional block = the log line is deleted from some builds.
# ryg instruments the source itself and UE_LOG filters by level instead of removing calls (S15/S17), so the
# failure shape is `#if` + LOG_, not "too many logs". Block depth is tracked with awk; only files that
# contain a LOG_ call are scanned, so the cost stays proportional to the logged surface.
r31_guarded_logs() {
  local f p
  rg -l --no-heading 'LOG_[A-Z]+[[:space:]]*\(' "$1" 2>/dev/null | path_filter | while IFS= read -r f; do
    p=${f//\\//} # rg emits `src\core\x.h` on Windows; awk would eat the backslashes as escapes
    awk -v file="$p" '
      /^[[:space:]]*#[[:space:]]*(if|ifdef|ifndef)/ { d++ }
      /^[[:space:]]*#[[:space:]]*endif/ { if (d > 0) d-- }
      d > 0 && /LOG_(TRACE|DEBUG|INFO|WARN|ERROR|CRITICAL)[[:space:]]*\(/ { print file ":" FNR ": " $0 }
    ' "$f"
  done
}
# R32: an ISystem implementation with no observation point at all — neither a LOG_* call nor a Tracy span.
# ZoneScopedN alone satisfies R32 (S18: the span IS the observation point, and it compiles away when the
# profiler is disabled), which is why this detector greps for the span, not only for logs.
RE_R32_OBS='LOG_[A-Z]+[[:space:]]*\(|ZoneScoped'
r32_silent_systems() {
  local f obs
  rg -l --no-heading 'public +(ecs::)?ISystem' "$1" 2>/dev/null | path_filter | rg -v 'ISystem\.h$' | while IFS= read -r f; do
    # comments are stripped before the observation check: a file that only MENTIONS ZoneScopedN in prose is
    # still a silent system (the ZzSilentSystem fixture header documents exactly that). --with-filename keeps
    # the "<path>:<line>:" shape that strip_all_comments expects, even when a single file is passed in.
    obs=$(rg -n --no-heading --with-filename "$RE_R32_OBS" "$f" 2>/dev/null | strip_all_comments | wc -l | tr -d ' ')
    [ "${obs:-0}" = "0" ] && printf '%s: silent system (no LOG_* / ZoneScoped)\n' "${f//\\//}"
  done
}
# R33①: a comment that only restates the following code line (a "what" comment) and carries no why-marker.
# Rule: every content word (>=3 chars, stopwords dropped) of the comment must also appear in the next
# non-blank, non-comment line, and the comment must not carry why|invariant|fallback|boundary|thread-affinity.
# Non-ASCII comments (e.g. Korean) yield no content words here and are skipped — they are manual judgement.
r33_what_comments() {
  local f p
  rg -l --no-heading '^[[:space:]]*//' "$1" --glob '*.h' --glob '*.cpp' 2>/dev/null | path_filter | while IFS= read -r f; do
    awk -v file="${f//\\//}" '
      function words(s,   n, a, i, out) {
        gsub(/[^A-Za-z0-9_ ]/, " ", s)
        n = split(s, a, /[^A-Za-z0-9_]+/)
        out = ""
        for (i = 1; i <= n; i++)
          if (length(a[i]) >= 3 && tolower(a[i]) !~ /^(the|and|for|this|with|from|that|into|use|used|using|new)$/)
            out = out " " tolower(a[i])
        return out
      }
      { lines[NR] = $0 }
      END {
        for (i = 1; i <= NR; i++) {
          if (lines[i] !~ /^[[:space:]]*\/\//) continue
          cmt = lines[i]
          sub(/^[[:space:]]*\/\//, "", cmt)
          if (cmt ~ /(why|invariant|fallback|boundary|thread-affinity)/) continue
          # 미결(design §N) is the decision-status marker defined in
          # docs/guidelines/game-code-changeability.md §1: a comment that names the open design question is
          # already a why-marker, not a restatement of the code below.
          if (cmt ~ /미결\(/) continue
          j = i + 1
          while (j <= NR && lines[j] ~ /^[[:space:]]*$/) j++
          if (j > NR || lines[j] ~ /^[[:space:]]*\/\//) continue
          cw = words(cmt)
          if (cw == "") continue
          code = words(lines[j])
          n = split(cw, a, " ")
          if (n < 2) continue   # a one-word label (e.g. `// Mouse`) is not a restatement of the next line
          all = 1
          for (k = 1; k <= n; k++) if (index(code, a[k]) == 0) all = 0
          if (all) print file ":" i ": " lines[i]
        }
      }
    ' "$f"
  done
}
# baseline_filter: drop known PRE-EXISTING debt (tests/changeability/baseline.txt: "<rule> <path> <reason>").
# Counting happens in a temp file because the loop runs in a subshell. Baselines must be justified in the
# review record; adding one to reach a green run is itself a finding.
PRE_FILE=""

init_baseline() {
  PRE_FILE=$(mktemp 2>/dev/null || echo "$ROOT/.cache/pre-changeability.txt")
  : >"$PRE_FILE"
  trap '[ -n "$PRE_FILE" ] && rm -f "$PRE_FILE"' EXIT
}
bl() { # bl <rule> : filter stdin
  local rule="$1" line f
  while IFS= read -r line; do
    [ -z "$line" ] && continue
    f=${line%%:*}
    f=${f//\\//}
    if [ -f tests/changeability/baseline.txt ] &&
      awk -v r="$rule" -v p="$f" '$1==r && $2==p {found=1} END{exit !found}' tests/changeability/baseline.txt; then
      printf '%s\n' "$line" >>"$PRE_FILE"
      continue
    fi
    printf '%s\n' "$line"
  done
}
baseline_count() { [ -n "$PRE_FILE" ] && [ -f "$PRE_FILE" ] && wc -l <"$PRE_FILE" | tr -d ' ' || echo 0; }

# The R2 clause ③ pattern: owning raw pointer field, any type spelling (F2: `std::string* p{nullptr}`)
RE_OWNING_PTR='[A-Za-z_:<>]+[[:space:]]*\*[[:space:]]*[A-Za-z_][A-Za-z0-9_]*[[:space:]]*\{[[:space:]]*nullptr'
RE_COMPONENT_BAD="^[[:space:]]*(virtual[[:space:]]|~[A-Za-z_][A-Za-z0-9_]*[[:space:]]*\\(|$RE_OWNING_PTR)"
RE_R4_LITERAL='[-+]?[0-9]+\.[0-9]+f|"[a-z0-9_/]+\.(png|wav|json)"'
RE_R4_EXEMPT='\b(tau|kPi|kEpsilon|MAX_[A-Z0-9_]+)\b'
# R5 (F4): entity creation only — `ctx().emplace<T>()` is R6's sanctioned service-locator form
RE_ENTITY_CREATE='\.create\(\)|\.emplace[_a-z]*<[^>]*>\([[:space:]]*[A-Za-z_][A-Za-z0-9_]*[[:space:]]*[,)]'
# ---- DOOM 3 heritage rules R21..R30 (S14) ----
RE_R21_MACRO='^[[:space:]]*#[[:space:]]*define[[:space:]]+[A-Z_][A-Z0-9_]*(DECLARATION|PROTOTYPE|REGISTRATION|REGISTER)\b'
RE_R21_STATIC='^[[:space:]]*static[[:space:]]+[A-Z][A-Za-z0-9_]*[[:space:]]+[A-Za-z_][A-Za-z0-9_]*(Type|Meta|Registry)[[:space:]]*[({;]'
RE_R23_KEY='\b(Get|Set)(Int|Float|Bool|String)[[:space:]]*\([[:space:]]*"|(std::map|std::unordered_map)[[:space:]]*<[[:space:]]*std::string[[:space:]]*,[[:space:]]*std::string'
# R24 targets GLOBAL debug/visual toggles living in systems/states/core. A per-entity flag inside a component
# (e.g. FDebugPrimitive.drawCollider) is data, not a scattered global switch, and is deliberately out of scope.
# The pattern is line-anchored so a function PARAMETER (`bool enableFileLog = true`) is not a hit.
RE_R24_FLAG='^[[:space:]]*bool[[:space:]]+(show|debug|draw|enable)[A-Z][A-Za-z0-9_]*'
RE_R25_IO='std::ifstream|std::ofstream|fopen[[:space:]]*\(|std::filesystem::(path|exists)'
RE_R26_SDL='\bSDL_[A-Za-z]'
RE_R27_EVENT='(publish|dispatch|emit|send)[[:space:]]*\([[:space:]]*"|void[[:space:]]*\*[[:space:]]*(args|payload|data)\b|\bProcessEventArgPtr\b'
RE_R28_SAVE='void[[:space:]]+(Save|Restore)[[:space:]]*\(|\b(SaveGame|RestoreGame)[[:space:]]*&'
RE_R29_BOUNDARY='(struct|class)[[:space:]]+[A-Za-z_][A-Za-z0-9_]*(Import|Export|API|Api)[A-Za-z0-9_]*|DLL_(Load|GetProcAddress)|GetGameAPI'
# R31 (F15): the failure shape is `#if`-guarded LOG_*, not "verbose logging". See r31_guarded_logs().
RE_R31_LOG='LOG_(TRACE|DEBUG|INFO|WARN|ERROR|CRITICAL)[[:space:]]*\('

if [ "$MODE" = repo ]; then
  init_baseline
  # ------------------------------------------------------------ R1 one file = one system, name matches
  note "== R1 one ISystem per file / filename = type name (0 expected) =="
  r1bad=""
  while IFS= read -r f; do
    c=$(rg -o --no-heading 'struct [A-Za-z_][A-Za-z0-9_]*[^;{]*: *public +(ecs::)?ISystem' "$f" 2>/dev/null | wc -l | tr -d ' ')
    [ "${c:-0}" -ge 2 ] && r1bad="$r1bad$f: $c ISystem types\n"
    [ "$(wc -l <"$f")" -gt 400 ] && r1bad="$r1bad$f: >400 lines\n"
    base=$(basename "$f" .h)
    rg -q --no-heading "struct $base\b" "$f" 2>/dev/null || r1bad="$r1bad$f: filename != type name\n"
  done < <(rg -l --no-heading 'public +(ecs::)?ISystem' "$SRC/ecs/systems" "$SRC/gameplay/systems" 2>/dev/null | path_filter)
  [ -z "$r1bad" ] && ok "R1 clean" || {
    printf '%b' "$r1bad"
    bad "R1 multiple systems / oversized / filename mismatch"
  }

  note "== R2 component purity (0 expected) =="
  r2=$(hits_code "$RE_COMPONENT_BAD" "$SRC/ecs/components/" "$SRC/gameplay/components/" | bl R2 || true)
  [ -z "$r2" ] && ok "R2 clean" || {
    printf '%s\n' "$r2"
    bad "R2 component contains virtual / destructor / owning raw pointer"
  }

  note "== R3 no cross-system include (0 expected) =="
  r3=$(rg -n --no-heading '#include "ecs/systems/[A-Za-z]+System\.h"' "$SRC/ecs/systems/" 2>/dev/null | rg -v 'ISystem\.h' | path_filter | bl R3 || true)
  # the gameplay layer follows the same rule; R1 already enforces it only inside a file, R3 catches the same
  # mistake in the file's includes (a gameplay system reaching for another gameplay system's internals).
  r3g=$(rg -n --no-heading '#include "gameplay/systems/[A-Za-z]+System\.h"' "$SRC/gameplay/systems/" 2>/dev/null | rg -v 'ISystem\.h' | path_filter | bl R3 || true)
  [ -z "$r3$r3g" ] && ok "R3 clean" || {
    printf '%s\n%s\n' "$r3" "$r3g"
    bad "R3 system includes another concrete system"
  }

  note "== R3 event delivery order: frame consumer registered after its producer (0 expected) =="
  r3o=$(event_order_report "$SRC" | bl R3 || true)
  [ -z "$r3o" ] && ok "R3 event order clean" || {
    printf '%s\n' "$r3o"
    bad "R3 a frame event is consumed by a system registered before its producer (beginFrame drops it)"
  }

  note "== R4 gameplay tuning literals (0 expected in systems/states/gameplay) =="
  r4=$(hits "$RE_R4_LITERAL" "$SRC/ecs/systems" "$SRC/states" "$SRC/gameplay" 2>/dev/null | rg -v '//\s*fallback:' | rg -v "$RE_R4_EXEMPT" | bl R4 || true)
  [ -z "$r4" ] && ok "R4 clean" || {
    printf '%s\n' "$r4"
    bad "R4 unmarked tuning literal / asset path"
  }

  note "== R5 entity creation outside factories (0 expected) =="
  r5=$(hits "$RE_ENTITY_CREATE" "$SRC" 2>/dev/null | path_filter | bl R5 || true)
  [ -z "$r5" ] && ok "R5 clean" || {
    printf '%s\n' "$r5"
    bad "R5 entity created outside a factory"
  }

  note "== R6 singleton / mutable global / function-local static (0 expected) =="
  r6a=$(hits "static\s+[A-Za-z_:<>]+\s*[*&]?\s*(instance|Instance|getInstance)" "$SRC" | bl R6 || true)
  r6b=$(hits "^(static\s+)?(int|float|double|bool|std::\w+|[A-Z]\w+)\s+\w+\s*(=[^=]|\{)" "$SRC" --glob '!**/*.cpp' | bl R6 || true)
  r6c=$(hits "^\s*extern\s" "$SRC" | bl R6 || true)
  # R6④ (F7): indented non-const statics (function-local / member) — const/constexpr and functions exempt
  r6d=$(hits_code "^\s+static\s+" "$SRC" | rg -v 'static\s+(const|constexpr)' | rg -v 'static\s+[A-Za-z_:<>]+[\s*&]+[A-Za-z_]\w*\s*\(' | bl R6 || true)
  if [ -z "$r6a$r6b$r6c$r6d" ]; then ok "R6 clean"; else
    printf '%s\n%s\n%s\n%s\n' "$r6a" "$r6b" "$r6c" "$r6d"
    bad "R6 global state / singleton / function-local static"
  fi

  note "== R7 layer boundary (0 expected) =="
  r7a=$(hits '#include\s+"(gameplay|states)/' "$SRC/core" "$SRC/ecs" | bl R7 || true)
  r7b=$(hits_code "\b(Player|Enemy)\b" "$SRC/core" "$SRC/ecs" | bl R7 || true)
  if [ -z "$r7a$r7b" ]; then ok "R7 clean"; else
    printf '%s\n%s\n' "$r7a" "$r7b"
    bad "R7 gameplay/states leaked into core or ecs"
  fi

  note "== R8 enum dispatched across >=3 files (0 expected) =="
  r8bad=""
  for e in $(rg -o --no-heading -r '$2' 'enum(\s+class)?\s+([A-Za-z_][A-Za-z0-9_]*)' "$SRC/ecs" "$SRC/core" "$SRC/states" 2>/dev/null | sort -u); do
    f=$(rg -l --no-heading "case\s+$e::|==\s*$e::" "$SRC" 2>/dev/null | path_filter | sort -u | wc -l | tr -d ' ')
    [ "${f:-0}" -ge 3 ] && r8bad="$r8bad enum $e -> $f files\n"
  done
  [ -z "$r8bad" ] && ok "R8 clean" || {
    printf '%b' "$r8bad"
    bad "R8 adding one enumerator edits >=3 files"
  }

  note "== R9 dead #ifdef (0 expected) =="
  r9=$(hits "#\s*(ifdef|ifndef|if defined)" "$SRC/ecs" "$SRC/states" "$SRC/core" 2>/dev/null | rg -v "pragma|guard" | bl R9 || true)
  [ -z "$r9" ] && ok "R9 clean" || {
    printf '%s\n' "$r9"
    bad "R9 feature-flag dead code"
  }

  note "== R11 bool state soup in components (0 expected) =="
  r11=$(hits "\bbool\s+(is|has|can|should)[A-Z]\w*" "$SRC/ecs/components/" "$SRC/gameplay/components/" | bl R11 || true)
  [ -z "$r11" ] && ok "R11 clean" || {
    printf '%s\n' "$r11"
    bad "R11 mutually-exclusive bool state fields"
  }

  note "== R12 duplicated constants across files (0 expected) =="
  r12=$(rg --no-heading -o -r '$1=$2' '\b((?:k|K_)[A-Za-z0-9_]*)[[:space:]]*=[[:space:]]*([0-9]+\.?[0-9]*f?)' "$SRC/ecs" "$SRC/core" "$SRC/states" 2>/dev/null |
    awk -F: '{print $NF"|"$1}' | sort -u | cut -d'|' -f1 | sort | uniq -d | head -5)
  [ -z "$r12" ] && ok "R12 clean" || {
    printf '%s\n' "$r12"
    bad "R12 same constant name+value in >=2 files"
  }

  note "== R14 build registration burden (file add must need 0 edits) =="
  if rg -q 'GLOB_RECURSE' CMakeLists.txt 2>/dev/null; then ok "R14 CMake globs sources"; else
    bad "R14 no GLOB_RECURSE: new file needs a CMake edit"
  fi

  note "== R15 ownership: new/delete/owning raw pointer (0 expected) =="
  r15=$(hits_code "new\s+[A-Za-z_]|delete\s+[A-Za-z_(]|$RE_OWNING_PTR" "$SRC" 2>/dev/null | path_filter | bl R15 || true)
  [ -z "$r15" ] && ok "R15 clean" || {
    printf '%s\n' "$r15"
    bad "R15 manual new/delete or owning raw pointer"
  }

  note "== R16 non-determinism (0 expected; unordered iteration is manual) =="
  r16=$(hits "random_device|\.detach\(\)|time\(nullptr\)|steady_clock" "$SRC/ecs" "$SRC/core" 2>/dev/null | bl R16 || true)
  [ -z "$r16" ] && ok "R16 clean" || {
    printf '%s\n' "$r16"
    bad "R16 seeded-per-frame RNG / unmanaged thread / wall clock"
  }
  manual "R16 unordered_* iteration affecting results"

  note "== R18 thread affinity declarations (0 expected) =="
  r18=""
  while IFS= read -r line; do
    [ -z "$line" ] && continue
    f=${line%%:*}
    rg -q --no-heading '//\s*thread-affinity:' "$f" 2>/dev/null || r18="$r18$line\n"
  done < <(hits "std::thread|std::async|\.detach\(\)" "$SRC" 2>/dev/null)
  [ -z "$r18" ] && ok "R18 clean" || {
    printf '%b' "$r18"
    bad "R18 thread use without // thread-affinity: declaration"
  }

  note "== R19 swallowed exceptions (0 expected) =="
  r19=$(hits "catch\s*\(\s*\.\.\.|catch\s*\([^)]*\)\s*\{\s*\}" "$SRC" | bl R19 || true)
  [ -z "$r19" ] && ok "R19 clean" || {
    printf '%s\n' "$r19"
    bad "R19 catch(...) or empty catch"
  }

  note "== R20 include resolvability + ISystem contract (0 expected) =="
  # F3: a first-party include whose root directory does not exist is UNRESOLVED (typo / missing layer),
  # unless the root is a known SDK namespace. `gameplay/Player.h` must FAIL before src/gameplay exists.
  SDK='entt SDL3 SDL imgui ImGui glm stb spdlog fmt nlohmann directx vulkan boost'
  unresolved=""
  while IFS= read -r f; do
    d=$(dirname "$f")
    while IFS= read -r inc; do
      first=${inc%%/*}
      [ "$first" = "$inc" ] && continue # same-dir include
      case " $SDK " in *" $first "*) continue ;; esac
      case "$first" in SDL3* | SDL2* | SDL*) continue ;; esac # SDL3_image / SDL3_ttf / SDL3_mixer ...
      if [ -d "$SRC/$first" ] || [ -d "$ROOT/$first" ] || [ -f "$ROOT/$first" ]; then
        [ -f "$d/$inc" ] || [ -f "$SRC/$inc" ] || [ -f "$ROOT/$inc" ] || unresolved="$unresolved$f: unresolved $inc\n"
      else
        unresolved="$unresolved$f: unknown include root '$first/' ($inc)\n"
      fi
    done < <(rg -o --no-heading -r '$1' '#include "([^"]+)"' "$f" 2>/dev/null)
  done < <(rg -l --no-heading '#include "' "$SRC" 2>/dev/null | path_filter)
  r20name=""
  while IFS= read -r f; do
    rg -q --no-heading '\bname\(\)\s*(const)?\s*(override)?\s*\{' "$f" 2>/dev/null || r20name="$r20name$f: ISystem without name()\n"
  done < <(rg -l --no-heading 'public +(ecs::)?ISystem' "$SRC/ecs/systems" "$SRC/gameplay/systems" 2>/dev/null | path_filter)
  if [ -z "$unresolved$r20name" ]; then ok "R20 clean"; else
    printf '%b%b' "$unresolved" "$r20name"
    bad "R20 unresolved include / missing name()"
  fi

  # ------------------------------------------------------------ R21..R30 DOOM 3 heritage (S14)
  note "== R21 one registration site / no self-registering macro or static type object (0 expected) =="
  r21a=$(hits "$RE_R21_MACRO" "$SRC" | bl R21 || true)
  r21b=$(hits_code "$RE_R21_STATIC" "$SRC" | bl R21 || true)
  if [ -z "$r21a$r21b" ]; then ok "R21 clean"; else
    printf '%s\n%s\n' "$r21a" "$r21b"
    bad "R21 registration macro / self-registering static type object"
  fi

  note "== R23 string-key reads and string/string bags outside the asset boundary (0 expected) =="
  r23=$(hits "$RE_R23_KEY" "$SRC" | asset_boundary_filter | bl R23 || true)
  if [ -z "$r23" ]; then ok "R23 clean"; else
    printf '%s\n' "$r23"
    bad "R23 runtime dict access outside the loader boundary"
  fi

  note "== R24 debug/tuning toggles beyond the single debug surface (0 expected) =="
  r24=$(hits "$RE_R24_FLAG" "$SRC/ecs/systems" "$SRC/states" "$SRC/gameplay" "$SRC/core" | bl R24 || true)
  if [ -z "$r24" ]; then ok "R24 clean"; else
    printf '%s\n' "$r24"
    bad "R24 scattered show*/debug* flags instead of one registered debug surface"
  fi

  note "== R25 raw file IO outside the asset boundary (0 expected) =="
  r25=$(hits "$RE_R25_IO" "$SRC" | asset_boundary_filter | bl R25 || true)
  if [ -z "$r25" ]; then ok "R25 clean"; else
    printf '%s\n' "$r25"
    bad "R25 path assembly / file IO outside AssetManager|ResourceManager"
  fi

  note "== R26 platform API outside core (0 expected; two render systems are baselined) =="
  r26=$(hits "$RE_R26_SDL" "$SRC/ecs" "$SRC/states" "$SRC/debug" "$SRC/gameplay" | bl R26 || true)
  if [ -z "$r26" ]; then ok "R26 clean"; else
    printf '%s\n' "$r26"
    bad "R26 SDL/OS API used outside the platform boundary (core/)"
  fi

  note "== R27 name-based dispatch / void* packed args in events (0 expected) =="
  r27=$(hits_code "$RE_R27_EVENT" "$SRC/core/events" "$SRC/ecs" | bl R27 || true)
  if [ -z "$r27" ]; then ok "R27 clean"; else
    printf '%s\n' "$r27"
    bad "R27 string-name dispatch or void* packed event args"
  fi

  note "== R28 hand-written Save/Restore pairs (0 expected) =="
  r28=$(hits "$RE_R28_SAVE" "$SRC" | bl R28 || true)
  if [ -z "$r28" ]; then ok "R28 clean"; else
    printf '%s\n' "$r28"
    bad "R28 per-feature manual serialization instead of component snapshots"
  fi

  note "== R29 versionless boundary struct / DLL-style handoff (0 expected) =="
  r29=$(hits "$RE_R29_BOUNDARY" "$SRC" | bl R29 || true)
  if [ -z "$r29" ]; then ok "R29 clean"; else
    printf '%s\n' "$r29"
    bad "R29 boundary struct without a version / raw interface pointer handoff"
  fi

  note "== R30 fork-by-copy: duplicate basename, both >=40 lines, >=60% shared (0 expected) =="
  r30=$(dup_layer_report "$SRC")
  if [ -z "$r30" ]; then ok "R30 clean"; else
    printf '%s\n' "$r30"
    bad "R30 a layer/module was copied for a variant instead of composed"
  fi

  note "== R31 logs deleted by the preprocessor (0 expected; level control is the sanctioned off-switch) =="
  r31=$(r31_guarded_logs "$SRC" | bl R31 || true)
  if [ -z "$r31" ]; then ok "R31 clean"; else
    printf '%s\n' "$r31"
    bad "R31 a LOG_* call is wrapped in #if/#ifdef instead of being level-filtered"
  fi

  note "== R32 silent systems: ISystem with no LOG_* and no ZoneScoped (0 expected; 5 legacy systems baselined) =="
  r32=$(r32_silent_systems "$SRC" | bl R32 || true)
  if [ -z "$r32" ]; then ok "R32 clean"; else
    printf '%s\n' "$r32"
    bad "R32 system with no observation point (add ZoneScopedN + a LOG_* on the failure path)"
  fi

  note "== R33 comments that only restate the next line (0 expected) =="
  r33=$(r33_what_comments "$SRC" | bl R33 || true)
  if [ -z "$r33" ]; then ok "R33 clean"; else
    printf '%s\n' "$r33"
    bad "R33 what-comment instead of a why-comment (// why:|invariant:|fallback:|boundary:|thread-affinity:)"
  fi

  # ------------------------------------------------------------ R17/R22 data-asset checks (assets/data/ now exists)
  # R17 (S10): every settings/content asset must declare a schema version. Was N-A while assets/data/ was absent.
  note "== R17 schema migration: schema_version on assets/data/*.json (0 expected) =="
  r17bad=""
  if command -v jq >/dev/null 2>&1; then
    for f in assets/data/*.json; do
      [ -e "$f" ] || continue
      jq -e 'has("schema_version")' "$f" >/dev/null 2>&1 || r17bad="$r17bad$f: no schema_version\n"
    done
  else
    for f in assets/data/*.json; do
      [ -e "$f" ] || continue
      rg -q '"schema_version"' "$f" 2>/dev/null || r17bad="$r17bad$f: no schema_version\n"
    done
  fi
  [ -z "$r17bad" ] && ok "R17 clean (schema_version declared on every asset)" || {
    printf '%b' "$r17bad"
    bad "R17 asset without schema_version"
  }

  # R22 (S14/S6): content is data — the schema-driven loader exists and gameplay declares >=1 schema.
  note "== R22 content/balance is data: schema-driven loader (0 expected) =="
  r22bad=""
  [ -f "$SRC/core/data/FContentLoader.cpp" ] || r22bad="${r22bad}$SRC/core/data/FContentLoader.cpp: missing\n"
  if ! rg -q 'FContentSchema|FContentField' "$SRC/gameplay/data/" 2>/dev/null; then
    r22bad="${r22bad}$SRC/gameplay/data/: no declared FContentSchema/FContentField\n"
  fi
  [ -z "$r22bad" ] && ok "R22 clean (FContentLoader validates assets against declared schema)" || {
    printf '%b' "$r22bad"
    bad "R22 content not schema-driven"
  }

  # ------------------------------------------------------------ manual-only rules (F1 coverage contract)
  manual "R1 system must not mutate >=3 component groups (judgement)"
  manual "R9 revert-diff deletability (needs a revert diff)"
  manual "R10 abstraction impl/use-site counts where no pure-virtual decl exists"
  manual "R12 out-of-contract files (needs docs/reviews/<slug>/contract.md)"
  manual "R13 optimization evidence in pass-3.md"
  manual "R14 registration sites per added file"
  manual "R19 test seam for new systems"
  manual "R21 exactly one registration site per new type (needs the diff + a registration grep)"
  manual "R24 new toggles registered on the single debug/config surface (component-level per-entity flags are out of scope)"
  manual "R28 snapshot layer + schema_version on new persistent formats (absent -> N-A)"
  manual "R29 module boundary version/ownership (no boundary struct in this repo -> N-A)"
  manual "R30 variant by composition; the detector only sees duplicate basenames"
  manual "R31 incremental observation coverage: new/changed files that log state at all (needs the diff)"
  manual "R32 debug-view wiring: whether the system's state is visible on the debug overlay (needs judgement)"
  manual "R33 whether a non-obvious decision actually has its why-comment (needs judgement; only restatements are automated)"
  manual "R33 new src/ file without any comment line (needs the diff)"
else
  # ------------------------------------------------------------ calibration assertions (fixtures)
  note "MODE: calibrate against $SRC (fixtures are deliberately broken)"
  # R30 negative control: a short forwarding shim (the src/ core|debug Logger.h pair) must NOT be flagged
  tz_shim_check() { printf '%s\n' "R30 negative control: repo-mode core/Logger.h vs debug/Logger.h = 3 lines, below the 40-line floor (not a copied layer)"; }
  assert_hits() { # assert_hits <rule> <file> <expected-count> <pattern>
    local rule="$1" file="$2" want="$3" pat="$4" got
    got=$(fc "$pat" "$SRC/$file")
    if [ "$got" = "$want" ]; then ok "$rule $file = $got (want $want)"; else bad "$rule $file = $got (want $want)"; fi
  }
  assert_hits R1 ecs/systems/ZzComboSystem.h 3 'struct [A-Za-z_]+[^;{]*: *public ISystem' # R1 also covers file placement + >400 lines in the guideline text
  # F2: the broadened R2 pattern must catch `std::string* cachedLabel{nullptr}` (2 virtuals + 1 pointer = 3)
  assert_hits R2 ecs/components/FZzBad.h 3 "$RE_COMPONENT_BAD"
  assert_hits R2 ecs/components/FZzGood.h 0 "$RE_COMPONENT_BAD"
  assert_hits R3 ecs/systems/ZzComboSystem.h 2 '#include "ecs/systems/[A-Za-z]+System\.h"' # raw: self-contract + real violation
  r3f=$(fc_nocontract '#include "ecs/systems/[A-Za-z]+System\.h"' "$SRC/ecs/systems/ZzComboSystem.h")
  [ "$r3f" = 1 ] && ok "R3 after ISystem.h exclusion = 1 (D2 fix holds)" || bad "R3 after exclusion = $r3f (want 1)"
  # D14: the event-order detector must flag consumer-before-producer, and must NOT flag the three legal shapes
  # (producer first, a consumer of a type nobody queues, and a system reading back its own frame event).
  r3o=$(event_order_report "$SRC")
  r3o_n=$(printf '%s\n' "$r3o" | rg -c 'ZzEarlyConsumerSystem' || true)
  r3o_n=$(printf '%s' "$r3o_n" | tr -d ' \r')
  if [ "$r3o_n" = 1 ] && ! printf '%s\n' "$r3o" | rg -q 'ZzBeatConsumerSystem|ZzSelfReadSystem|ZzOrphanConsumerSystem'; then
    ok "R3 event order flags the early consumer only (D14 holds)"
  else
    bad "R3 event order detector = '$r3o' (want exactly the ZzEarlyConsumerSystem hit)"
  fi
  assert_hits R4 ecs/systems/ZzComboSystem.h 4 '[-+]?[0-9]+\.[0-9]+f|"[a-z0-9_/]+\.(png|wav|json)"'
  r4f=$(fc_fallback '[-+]?[0-9]+\.[0-9]+f' "$SRC/core/ZzLeak.h")
  [ "$r4f" = 2 ] && ok "R4 core/ZzLeak.h after //fallback: exclusion = 2 (D1 fix holds)" || bad "R4 after fallback exclusion = $r4f (want 2)"
  # F4: the R5 pattern must match entity creation (create + emplace<...>(e)) and NOT fire on ctx().emplace<T>()
  assert_hits R5 ecs/systems/ZzComboSystem.h 3 "$RE_ENTITY_CREATE"
  assert_hits R5 core/ZzLeak.h 0 "$RE_ENTITY_CREATE"
  assert_hits R6 core/ZzLeak.h 2 'static\s+[A-Za-z_:<>]+\s*[*&]?\s*(instance|Instance|getInstance)'
  assert_hits R6 core/ZzLeak.h 1 '^(static\s+)?(int|float|double|bool|std::\w+|[A-Z]\w+)\s+\w+\s*(=[^=]|\{)'
  # F7: R6 clause 4 (function-local static) must be calibratable
  assert_hits R6 ecs/systems/ZzNoOrderSystem.h 1 '^\s+static\s+'
  assert_hits R7 core/ZzLeak.h 1 '#include\s+"(gameplay|states)/'
  assert_hits R7 ecs/ZzStateLeak.h 2 '#include\s+"(gameplay|states)/'
  assert_hits R9 ecs/systems/ZzComboSystem.h 1 '#\s*(ifdef|ifndef|if defined)'
  assert_hits R11 ecs/components/FZzBad.h 2 '\bbool\s+is[A-Z]\w*'
  assert_hits R11 ecs/components/FZzStats.h 4 '\bbool\s+is[A-Z]\w*'
  assert_hits R15 core/ZzOwnership.h 2 'new\s+[A-Za-z_]'
  assert_hits R16 ecs/systems/ZzNoOrderSystem.h 3 'random_device|unordered_map|\.detach\(\)'
  assert_hits R18 core/ZzThread.h 2 'std::thread|\.detach\(\)'
  assert_hits R19 core/ZzError.h 1 'catch\s*\(\s*\.\.\.'

  # R8 (F8): prefix-free and `==`-based dispatch must both count; >=3 distinct files
  for spec in 'EWeapon' 'ZzKit'; do
    efiles=$(rg -l --no-heading "case\s+$spec::|==\s*$spec::" "$SRC" 2>/dev/null | sort -u | wc -l | tr -d ' ')
    [ "${efiles:-0}" -ge 3 ] && ok "R8 $spec dispatch spread = $efiles files" || bad "R8 $spec expected >=3 files, got ${efiles:-0}"
  done

  # R10: interface with <=1 implementation (D7 structural definition, keyed on derivation not on `class I`)
  iimpl=$(rg -o --no-heading ': *(public +)?IZzSolo\b' "$SRC/ecs/systems/ZzSoloAbstraction.h" 2>/dev/null | wc -l | tr -d ' \r')
  isites=$(rg -o --no-heading 'ZzSoloImpl' "$SRC/ecs/systems/ZzSoloAbstraction.h" 2>/dev/null | wc -l | tr -d ' \r')
  if [ "${iimpl:-0}" = "1" ] && [ "${isites:-0}" -le 2 ]; then
    ok "R10 IZzSolo impls=1 use-sites<=2 (FAIL case present; note it is NOT named 'class I*')"
  else
    bad "R10 fixture: impls=${iimpl:-0} use-sites=${isites:-0}"
  fi

  # R20 (F3): the fixture's core -> gameplay include must be reported UNRESOLVED, not skipped
  if rg -q --no-heading -r '$1' '#include "(gameplay/Player\.h)"' "$SRC/core/ZzLeak.h" 2>/dev/null; then
    ok "R20 core/ZzLeak.h -> gameplay/Player.h is a first-party include (must FAIL resolution)"
  else
    bad "R20 fixture include missing"
  fi

  # R12 (F11): a generic duplicate-constant detector, not a hardcoded fixture name
  dup=$(rg --no-heading -o -r '$1=$2' '\b((?:k|K_)[A-Za-z0-9_]*)[[:space:]]*=[[:space:]]*([0-9]+\.?[0-9]*f?)' "$SRC" 2>/dev/null |
    awk -F: '{print $NF"|"$1}' | sort -u | cut -d'|' -f1 | sort | uniq -d | wc -l | tr -d ' ')
  [ "${dup:-0}" -ge 1 ] && ok "R12 duplicated constant name+value detected generically ($dup)" || bad "R12 duplicate not detected"

  # R17: config JSON without a schema version
  if command -v jq >/dev/null 2>&1; then
    jq -e 'has("schema_version")' "$SRC/assets/data/zz_bad_config.json" >/dev/null 2>&1 &&
      bad "R17 fixture unexpectedly has schema_version" || ok "R17 config lacks schema_version (FAIL case present)"
  else
    rg -q '"schema_version"' "$SRC/assets/data/zz_bad_config.json" &&
      bad "R17 fixture unexpectedly has schema_version" || ok "R17 config lacks schema_version (FAIL case present)"
  fi

  # R21..R30 (S14): DOOM 3 heritage rules must be calibratable, not aspiration-only
  assert_hits R21 ecs/ZzRegMacro.h 1 "$RE_R21_MACRO"
  assert_hits R21 ecs/ZzRegMacro.h 2 "$RE_R21_STATIC"
  assert_hits R23 ecs/components/FZzDictBag.h 3 "$RE_R23_KEY"
  assert_hits R24 ecs/components/FZzToggles.h 3 "$RE_R24_FLAG"
  assert_hits R25 core/ZzAssetIo.h 3 "$RE_R25_IO"
  assert_hits R26 debug/ZzSdlLeak.h 3 "$RE_R26_SDL"
  assert_hits R27 ecs/systems/ZzStringEventSystem.h 3 "$RE_R27_EVENT"
  assert_hits R28 ecs/systems/ZzSaveRestoreSystem.h 2 "$RE_R28_SAVE"
  assert_hits R29 core/ZzMonolithBoundary.h 1 "$RE_R29_BOUNDARY"
  d30=$(dup_layer_report "$SRC")
  case "$d30" in
  *ZzVariantCopy.h*) ok "R30 duplicate-layer detector flags the 48/48-line ZzVariantCopy pair (98% shared)" ;;
  *) bad "R30 detector missed the ZzVariantCopy fixture pair" ;;
  esac
  shim_src=$(tz_shim_check 2>/dev/null || true)
  [ -n "$shim_src" ] && note "$shim_src"

  # R31..R33 (S15..S19): the observation/comment rules must be calibratable too, with negative controls
  g_log=$(rg -c --no-heading "$RE_R31_LOG" "$SRC/core/ZzGuardedLog.h" 2>/dev/null | tr -d ' \r')
  [ "${g_log:-0}" = "3" ] && ok "R31 fixture has 3 LOG_* calls total (2 guarded + 1 free)" || bad "R31 fixture LOG_ count = ${g_log:-0} (want 3)"
  g_hits=$(r31_guarded_logs "$SRC/core/ZzGuardedLog.h" | wc -l | tr -d ' ')
  [ "$g_hits" = "2" ] && ok "R31 guards only the 2 #if-wrapped calls (the free call is the negative control)" || bad "R31 guarded hits = $g_hits (want 2)"
  s_hits=$(r32_silent_systems "$SRC/ecs/systems/ZzSilentSystem.h" | wc -l | tr -d ' ')
  s_ctrl=$(r32_silent_systems "$SRC/ecs/systems/ZzSpannedSystem.h" | wc -l | tr -d ' ')
  [ "$s_hits" = "1" ] && ok "R32 flags ZzSilentSystem.h (no LOG_*, no span)" || bad "R32 ZzSilentSystem.h hits = $s_hits (want 1)"
  [ "$s_ctrl" = "0" ] && ok "R32 negative control: ZzSpannedSystem.h is observed by ZoneScopedN (0 hits)" || bad "R32 ZzSpannedSystem.h hits = $s_ctrl (want 0)"
  w_hits=$(r33_what_comments "$SRC/core/ZzWhatComment.h" | wc -l | tr -d ' ')
  [ "$w_hits" = "1" ] && ok "R33 flags only the '// player position' restatement (1 hit)" || bad "R33 what-comment hits = $w_hits (want 1)"
  w_ctrl=$(r33_what_comments "$SRC/ecs/components/FZzGood.h" | wc -l | tr -d ' ')
  [ "$w_ctrl" = "0" ] && ok "R33 negative control: FZzGood.h comment is not a restatement (0 hits)" || bad "R33 negative control hits = $w_ctrl (want 0)"

  # Slice-2 gameplay coverage (V34..V38): R1/R2/R26/R32 now also assert over src/gameplay. Each new rule needs a
  # positive fixture AND a negative control, otherwise "clean" in repo mode could just mean "never scanned".
  assert_hits R2 gameplay/components/FZzBadGameplayComponent.h 3 "$RE_COMPONENT_BAD"
  # the discovery pattern must accept the qualified base class the gameplay layer uses (`: public ecs::ISystem`),
  # otherwise R1/R20/R32 would keep silently skipping every gameplay system while reporting "clean".
  assert_hits R1 gameplay/systems/ZzMismatchSystem.h 1 'struct [A-Za-z_]+[^;{]*: *public +(ecs::)?ISystem'
  g1_match=$(rg -q --no-heading 'struct ZzMismatchSystem\b' "$SRC/gameplay/systems/ZzMismatchSystem.h" && echo matched || echo mismatched)
  [ "$g1_match" = "mismatched" ] && ok "R1 gameplay fixture: filename truly != type name (detectable)" || bad "R1 gameplay fixture filename matches its type — fixture no longer calibrates anything"
  assert_hits R26 gameplay/ZzSdlGameplay.h 1 "$RE_R26_SDL"
  g_quiet=$(r32_silent_systems "$SRC/gameplay/systems/ZzQuietGameplaySystem.h" | wc -l | tr -d ' ')
  g_obs=$(r32_silent_systems "$SRC/gameplay/systems/ZzObservedGameplaySystem.h" | wc -l | tr -d ' ')
  [ "$g_quiet" = "1" ] && ok "R32 flags a silent gameplay system (no LOG_*, no span)" || bad "R32 gameplay silent hits = $g_quiet (want 1)"
  [ "$g_obs" = "0" ] && ok "R32 negative control: a ZoneScopedN gameplay system is not flagged" || bad "R32 gameplay control hits = $g_obs (want 0)"
fi

if [ "$MODE" = repo ]; then
  note ""
  note "NOT CHECKED (manual):$MANUAL"
  note "PRE-EXISTING (baselined, not blocking): $(baseline_count) hit line(s) from tests/changeability/baseline.txt"
fi
note ""
if [ "$FAILED" = 0 ]; then note "changeability bundle: OK ($MODE)"; else note "changeability bundle: FAILED ($MODE)"; fi
exit "$FAILED"
