#!/usr/bin/env sh
# docs freshness + anchor checker for game01P.
# Enforces DOC_RULES.md R3 (verified anchors) and R4 (freshness header).
# Dependency-free POSIX sh: git, grep, sed, awk, cut, wc, mktemp.
#
# Anchor resolution runs on a copy of each doc with fenced code blocks (```...```)
# removed: fenced snippets contain commands and illustrative filenames that are
# not verifiable As-built claims. Prose anchors are what get resolved.
#
# Exit 0 on success, 1 on any failed check. Run from the repo root:
#   bash scripts/check-docs.sh
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT" || exit 1

MAX_AHEAD=20          # R4: staleness window in commit count
FAIL=0
DOC_COUNT=0
ANCHOR_COUNT=0

# fs-regex for a code-file anchor: path + optional :line or :line-line
ANCHOR_RE='[A-Za-z0-9_./-]+\.(h|cpp|hpp)(:[0-9]+(-[0-9]+)?)?'

# Flat list of every doc file (no subshell state loss).
DOC_FILES=$(find docs -name '*.md' 2>/dev/null | sort)

if [ -z "$DOC_FILES" ]; then
    echo "docs-check: FAIL (no docs/**/*.md files found)"
    exit 1
fi

# ---------------------------------------------------------------------------
for f in $DOC_FILES; do
    DOC_COUNT=$((DOC_COUNT + 1))
    line1=$(sed -n '1p' "$f")

    # R4: line 1 must be a doc-verify block with a 40-hex commit.
    commit=$(printf '%s\n' "$line1" | sed -n 's/.*commit=\([0-9a-fA-F]\{40\}\).*/\1/p')
    if [ -z "$commit" ]; then
        echo "FAIL: $f: line 1 is not a valid 'doc-verify' block (expected: <!-- doc-verify subsystem=<id> commit=<40-hex> date=YYYY-MM-DD -->)"
        FAIL=1
        continue
    fi
    date_ok=$(printf '%s\n' "$line1" | sed -n 's/.*date=\([0-9]\{4\}-[0-9]\{2\}-[0-9]\{2\}\).*/\1/p')
    if [ -z "$date_ok" ]; then
        echo "FAIL: $f: doc-verify block missing valid date=YYYY-MM-DD"
        FAIL=1
    fi

    # R4 freshness: commit must be an ancestor of HEAD and within MAX_AHEAD.
    if ! git merge-base --is-ancestor "$commit" HEAD >/dev/null 2>&1; then
        echo "FAIL: $f: verify commit $commit is not an ancestor of HEAD"
        FAIL=1
    else
        ahead=$(git rev-list --count "$commit..HEAD" 2>/dev/null)
        if [ "${ahead:-0}" -gt "$MAX_AHEAD" ]; then
            echo "FAIL: $f: verify commit $commit is $ahead commits behind HEAD (window=$MAX_AHEAD)"
            FAIL=1
        fi
    fi

    # R3: scan prose anchors (fenced blocks stripped) and resolve on-disk
    # paths / line bounds. Recovery refs (out/build, */tactical_d20/) are not
    # on disk; they are only valid when tagged [UNVERIFIED] (see audit below).
    tmp=$(mktemp)
    awk 'BEGIN{infence=0} /^```/{infence=!infence; next} !infence{print}' "$f" > "$tmp"

    toks=$(grep -oE "$ANCHOR_RE" "$tmp" | sort -u)
    for tok in $toks; do
        ANCHOR_COUNT=$((ANCHOR_COUNT + 1))

        case "$tok" in
            *:*) path=${tok%%:*}; linespec=${tok#*:} ;;
            *)   path=$tok; linespec="" ;;
        esac

        is_recovery=false
        case "$path" in
            out/*)            is_recovery=true ;;
            */tactical_d20/*) is_recovery=true ;;
            gameplay/*)       is_recovery=true ;;
            src/gameplay/*)   is_recovery=true ;;
        esac

        if $is_recovery; then
            # R2 audit: recovery refs MUST be tagged [UNVERIFIED] on their
            # line or the immediately preceding line.
            tagged=""
            hit_lines=$(grep -nF -- "$tok" "$tmp" | cut -d: -f1)
            for ln in $hit_lines; do
                if sed -n "${ln}p" "$tmp" | grep -q 'UNVERIFIED'; then tagged=1; break; fi
                prev=$((ln - 1))
                if [ "$prev" -ge 1 ] && sed -n "${prev}p" "$tmp" | grep -q 'UNVERIFIED'; then tagged=1; break; fi
            done
            if [ -z "$tagged" ]; then
                echo "FAIL: $f: recovery anchor '$tok' is not tagged [UNVERIFIED] on its line (R2)"
                FAIL=1
            fi
            continue
        fi

        # On-disk anchor: file must exist; line must be within file length.
        if [ -f "$path" ]; then
            if [ -n "$linespec" ]; then
                last="${linespec##*-}"           # range -> upper bound
                wc_lines=$(wc -l < "$path")
                if [ "${last:-0}" -gt "$wc_lines" ]; then
                    echo "FAIL: $f: anchor '$tok' line $last exceeds $path length ($wc_lines)"
                    FAIL=1
                fi
            fi
        else
            echo "FAIL: $f: anchor path '$path' not found on disk"
            FAIL=1
        fi
    done
    rm -f "$tmp"
done

# ---------------------------------------------------------------------------
if [ "$FAIL" -ne 0 ]; then
    echo "docs-check: FAIL ($DOC_COUNT docs, $ANCHOR_COUNT anchors resolved-checked)"
    exit 1
fi
echo "docs-check: OK ($DOC_COUNT docs, $ANCHOR_COUNT anchors resolved-checked)"
exit 0
