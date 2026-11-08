#pragma once

#include <expected>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace game {

/// WP-20: the permanent meta save (`meta_save.json`) schema version. It evolves independently from the run
/// save (kDungeonSaveVersion): a meta corruption resets seals/unlocks/roster/ladder only, never the run. A
/// document ABOVE this version is a future file — the loader must leave it untouched and make the session
/// read-only (FMetaProgress), never "reset and continue" (that would overwrite the file).
inline constexpr int kMetaSaveVersion = 1;

/// The milestone ledger key format (D-14/D-22 "first clear only"): `"<theme.id>/<dungeonInTheme>#clear"` or
/// `"...#boss"`. The engine's WP-20 rule allows dungeonInTheme 0 (0-based); the leading zero and the empty
/// theme are rejected. The key deliberately carries no cycle (D-22: repeated clears grant 0).
[[nodiscard]] bool IsValidMetaGrantedKey(std::string_view key);

/// A ladder rank key: a positive decimal integer string ("1", "42"; never "0" or "02"). WP-18 owns the rank
/// catalog (difficulty.json); the save only stores keys.
[[nodiscard]] bool IsValidMetaLadderRankKey(std::string_view key);

/// One identity row of the roster (L1). Identity only — name/trait/medals. There is no stat or power field
/// here and there never may be: D-9/D-10, R3 P07 and Q18 ("메타는 옵션을 팔고 파워를 팔지 않는다"), pinned
/// by the NoPowerFields test.
struct FMetaRosterRow {
    std::string id;
    std::string name;
    std::string trait;
    int medals{0};
    bool enabled{false};
    bool locked{false};
};

/// One ladder rank's progress (D-20: per-rank checkpoint; consumed by WP-18, stored here already).
struct FMetaLadderRank {
    int bestDungeon{0};
    int checkpoint{0};
    std::vector<std::string> granted;  // ledger: the milestone keys already granted at this rank
    int grand{0};                      // final-boss breakthroughs at this rank (first grant only)
};

/// The cross-run statistics block. Statistics count repeats (cycles included); grants never do.
struct FMetaStats {
    int runs{0};
    int clears{0};
    int bestDepth{0};
    int cycleBest{0};
    int grand{0};
};

/// The meta save document. Defaults are all-zero/empty; FMetaProgress fills the roster from the shipped
/// catalog and owns the ladder import. `savedAt` is stamped at write time (MetaUtcTimestamp).
struct FMetaSave {
    int schemaVersion{kMetaSaveVersion};
    std::string savedAt;
    int seals{0};
    std::vector<std::string> unlocks;
    std::vector<FMetaRosterRow> roster;
    std::map<std::string, FMetaLadderRank> ladder;
    FMetaStats stats;
};

[[nodiscard]] FMetaSave DefaultMetaSave();

/// The write-side serialization. It emits exactly the documented key set (schema_version, saved_at, seals,
/// unlocks, roster, ladder, stats) — the NoPowerFields contract lives here: the serializer can never emit
/// a stats/power key because there is no field for one.
[[nodiscard]] nlohmann::json MetaSaveToJson(const FMetaSave& save);

/// The read-side schema validation: structure, field types, non-negative counts, ladder-rank key format and
/// granted-key format; duplicates in `granted`/`unlocks`/`roster` are rejected. Unknown fields are ignored
/// (forward compatibility — a future additive field must not be mistaken for corruption). `schema_version`
/// must be exactly kMetaSaveVersion; the future-version branch is the caller's job (FMetaProgress reads the
/// raw version first and keeps the file read-only). Catalog cross-checks (unlocks ⊆ unlocks.json, roster id
/// ∈ roster.json) belong to FMetaProgress, which owns the loaded catalogs.
[[nodiscard]] std::expected<FMetaSave, std::string> MetaSaveFromJson(const nlohmann::json& document);

/// The `saved_at` stamp (UTC ISO-8601 seconds), same shape as the run save's.
[[nodiscard]] std::string MetaUtcTimestamp();

} // namespace game
