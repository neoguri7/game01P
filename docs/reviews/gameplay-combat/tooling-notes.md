# Tooling notes — gameplay-combat

## pi-lens (C/C++) diagnostics are not usable as a gate in this repo

While implementing slice 2 the editor-side lens check reported `🔴 bit fields should not be used` on files that
contain **no bit field**, and `file not found` on includes that the CMake build resolves (`src/` is an include
root: `target_include_directories(game01P PRIVATE "src")`).

Evidence collected on this branch:

```bash
# 0 bitfield declarations in every flagged file
grep -nE '^[[:space:]]*[A-Za-z_:<>0-9]+[[:space:]]+[A-Za-z_][A-Za-z0-9_]*[[:space:]]*:[[:space:]]*[0-9]+[[:space:]]*;' \
  src/core/data/FContentValue.h src/core/data/FContentLoader.h src/core/AssetManager.h src/core/data/FContentRow.h | wc -l
# → 0

# what the analyzer calls a "bit field" (src/core/data/FContentValue.h:13-18)
struct FContentValue {
    EContentFieldType type = EContentFieldType::Text;                                   # L14  ← scoped `::`
    std::variant<std::string, double, bool, std::vector<std::string>, std::vector<double>> value{std::string{}};  # L15
    [[nodiscard]] const std::string* asText() const { ... }                             # L17  ← `[[nodiscard]]`
};

# the flagged files are untouched by this slice
git status --short src/core/data/FContentValue.h src/core/data/FContentLoader.h src/core/AssetManager.h | wc -l
# → 0
```

Conclusion: the analyzer parses these headers with a C grammar (scoped `::`, `[[nodiscard]]`, default member
initializers) and cannot see the project include roots, so its findings are false positives. The flagged files are
slice-1 code that already passed review rounds 1–2.

**Gate for this slice** = `scripts/verify-changeability.sh` (rule harness) + the owner-run MSVC build
(`scripts/check-windows.ps1 -Configuration Debug`). The lens check is reported as `NOT USABLE` in the review
record instead of being treated as a pass/fail signal, so no finding is silently dropped and no already-reviewed
file is edited to satisfy a broken detector (R12②: the diff stays inside the contract's in-scope list).

## Definitive check (bitfield false positive)

```text
$ grep -cE '[A-Za-z_]+[[:space:]]+[A-Za-z_0-9]+[[:space:]]*:[[:space:]]*[0-9]+[[:space:]]*;' \
    src/core/data/FContentValue.h src/core/data/FContentLoader.h src/core/AssetManager.h
0
$ sed -n '14p;17p' src/core/data/FContentValue.h
    EContentFieldType type = EContentFieldType::Text;
    [[nodiscard]] const std::string* asText() const { return std::get_if<std::string>(&value); }
$ sed -n '18p' src/core/data/FContentLoader.h
    [[nodiscard]] static std::optional<std::vector<FContentRow>> load(const FAssetManager& assets, const FContentSchema& schema);
$ git diff --stat 6b432cd~1 6b432cd -- src/core/data/FContentValue.h src/core/data/FContentLoader.h src/core/AssetManager.h
(empty)
$ git status --porcelain src/core/data/FContentValue.h src/core/data/FContentLoader.h src/core/AssetManager.h
(empty)
```

Zero bit fields exist. The reported lines are an enumerator literally named `Text` (`EContentFieldType::Text`), a
`std::variant`, and a `[[nodiscard]]` attribute — i.e. the lens's C parser is recovering from a failed include
resolution and re-reading ordinary member syntax as a bitfield. Verdict: `NOT USABLE` on this branch. The correct
response is to report it, not to edit already-reviewed files (`src/core/data/*`, `src/core/AssetManager.h` are
outside this slice's contract and untouched by it).
