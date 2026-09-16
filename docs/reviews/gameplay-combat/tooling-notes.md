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
