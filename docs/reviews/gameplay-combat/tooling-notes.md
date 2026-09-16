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

## New harness rule: event queue/producer frame order (R3)

`scripts/verify-changeability.sh` gained an `event_order_report` pass after slice 2 shipped the cross-frame event
bug. `FEventBus::beginFrame()` is called once per frame from `src/core/Engine.cpp:248` and clears the frame queues,
so a system that reads an event is only correct if **every** producer of that type is registered *before* it in the
walk order. The pass resolves each `addSystem<T>()` index per registry file, resolves each system's header, and
reports a violation of the shape `consumer (index c) reads E, queued later by producer (index p)` with `p > c`.
The real bug it was written for: `FTurnStartSystem` read `FTurnEndRequestedEvent`, but the producers
(`FPlayerCommandSystem`, `FEnemyTurnSystem`) are registered after it, so the request was read a frame late and
silently dropped by the next `beginFrame()`.

Three calibration traps — all three first produced a wrong pass/fail verdict before being fixed, so they are
recorded here rather than rediscovered:

1. **Direction.** The first version iterated producers and flagged later *readers*; the violation is the opposite:
   the consumer's index is the lower one. A consumer that runs early sees an empty queue and the producer fills it
   afterwards, unread.
2. **`strip_comments` needs a `path:line:` prefix.** With a single file argument `rg` omits that prefix, so the
   comment stripper matched nothing and the fixture's `// systems.addSystem<...>` annotation lines were counted as
   real registrations. Fixed with `rg -n -H`.
3. **Per-system body scope.** R1 asks for one `ISystem` per file, but the detector must stay correct when a header
   holds helpers — or two fixture structs. Searching the whole header attributed one system's `queueFrame<E>` to its
   neighbour and produced two extra false positives. Fixed with a `system_body()` awk helper that slices
   `struct <name>` to its closing `};`.
   A fourth gotcha inside that helper: gawk/mawk read `\b` in a dynamic regex as a literal *backspace*, so the
   boundary must be written as `([^A-Za-z0-9_]|$)`; with `\b` the helper silently returned an empty body and the
   positive control vanished instead of failing loudly.

Calibration is part of the harness run: on `tests/changeability/fixtures/gameplay/**` the pass must report exactly
one violation (`ZzEarlyConsumerSystem` reading `ZzPingEvent`), while the real tree is R3-clean.

Guided question: *when a detector is written to catch a bug that already happened, does its own calibration
positive control actually fail when the bug is reintroduced — or does it pass for the wrong reason?*
