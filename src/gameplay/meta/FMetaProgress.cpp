#include "gameplay/meta/FMetaProgress.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <SDL3/SDL.h>

#include "core/AssetManager.h"
#include "core/JsonLoader.h"
#include "core/Logger.h"
#include "gameplay/dungeon/FDungeonRun.h"
#include "gameplay/dungeon/FDungeonSave.h"
#include "gameplay/dungeon/FRunEventLog.h"
#include "gameplay/dungeon/FRunSequence.h"

namespace game {

namespace {

/// WP-18 (W11) replaces this constant with its active-rank accessor — every W8 grant and the summary line
/// resolve the rank through it, so the switch happens in one place (cross-request #1).
constexpr int kDefaultLadderRank = 1;

/// The checkpoint cap: FRunFlow.cpp ContinueReward writes `min(run->cleared, 999)` into checkpoint.json
/// and the checkpoint loader rejects values above 999. The ladder's checkpoint/best_dungeon mirror that cap
/// (one constant for the import AND the live grant) so the dual record can never diverge at the ceiling
/// (depth reaches 1000 in LeaveCamp).
constexpr int kLadderCheckpointCap = 999;

constexpr const char* kSaveDirEnv = "GAME01P_SAVE_DIR";

/// The GAME01P_SAVE_DIR override, or nullopt when unset or empty. std::getenv, not SDL_getenv: SDL caches
/// the environment on first use, so a test that sets the variable later would not be seen (the same reader
/// as the dungeon init and the settings module use).
std::optional<std::filesystem::path> SaveDirOverride() {
    const char* value = nullptr;
#ifdef _MSC_VER
#pragma warning(suppress: 4996) // std::getenv: read-only, no buffer to overflow
#endif
    value = std::getenv(kSaveDirEnv);
    if (value == nullptr || *value == '\0') return std::nullopt;
    return std::filesystem::path(value);
}

std::optional<std::filesystem::path> PreferenceDirectory() {
    char* path = SDL_GetPrefPath("Game01P", "EmberDungeon");
    if (path == nullptr) return std::nullopt;
    std::filesystem::path result(path);
    SDL_free(path);
    return result;
}

/// One unlock catalog row (`unlocks.json`). `id` is the content id (skill.hook / rune.inferno / a roster
/// stub id) and is what meta_save.unlocks stores.
struct FMetaUnlockEntry {
    std::string id;
    std::string kind;
    int cost{0};
    int sealGate{0};
};

/// The shipped meta catalogs, loaded once at boot through the asset boundary.
struct FMetaCatalog {
    std::vector<FMetaRosterRow> roster;
    std::vector<FMetaUnlockEntry> unlocks;
};

} // namespace

/// The store plus this session's policy. `disabled` = never write the file this session (future version,
/// failed corruption backup, unusable storage); `reason` records why (the surface for logs and tests).
/// Declared in FMetaProgress.h; only this translation unit names its fields.
struct FMetaProgressStore {
    FMetaSave save;
    FMetaCatalog catalog;
    std::filesystem::path path;
    bool disabled{false};
    std::string reason;
    bool saveWarned{false};
    bool warnedNoSequence{false};
    bool warnedZeroDepth{false};
};

namespace {

/// Wiring marker: run-event listeners outlive the store (a new run keeps the subscribers), so the listener
/// must be registered once per registry and must survive ShutdownMetaProgress — a re-initialize after a
/// shutdown would otherwise add a second listener (lapsed-listener discipline). Never erased.
struct FMetaProgressWiring {
    bool listenerRegistered{false};
};

bool FileExists(const std::filesystem::path& path) {
    std::error_code code;
    return std::filesystem::exists(path, code) && !code;
}

std::string CheckpointPath(entt::registry& registry) {
    if (const auto* checkpoint = registry.ctx().find<FDungeonCheckpoint>()) return checkpoint->path;
    return {};
}

/// True when the raw document names a schema version above this build's. Read before any parse so a future
/// file is neither reset nor partially interpreted (R5: the session turns read-only instead).
bool FutureMetaVersion(const nlohmann::json& document) {
    if (!document.is_object()) return false;
    const auto version = document.find("schema_version");
    if (version == document.end() || !version->is_number_integer()) return false;
    if (version->is_number_unsigned()) {
        return version->get<std::uint64_t>() > static_cast<std::uint64_t>(kMetaSaveVersion);
    }
    return version->get<std::int64_t>() > static_cast<std::int64_t>(kMetaSaveVersion);
}

bool IsKnownUnlockKind(std::string_view kind) {
    return kind == "skill" || kind == "rune" || kind == "event" || kind == "roster";
}

/// An unlock id must belong to its declared kind's namespace — the save stores ids, and kind is what later
/// consumers switch on.
bool UnlockIdMatchesKind(std::string_view id, std::string_view kind) {
    if (kind == "skill") return id.starts_with("skill.");
    if (kind == "rune") return id.starts_with("rune.");
    if (kind == "event") return id.starts_with("event.");
    if (kind == "roster") return id.starts_with("party.");
    return false;
}

bool LoadRosterCatalog(const FAssetManager& assets, FMetaCatalog& catalog, std::string& error) {
    const auto path = assets.resolvePath("data/roster");
    if (!path) {
        error = "roster.json is missing";
        LOG_ERROR("meta content: roster.json is missing; the shipped roster is required for boot.");
        return false;
    }
    const auto document = FJsonLoader::Load(*path, 1);
    if (!document) {
        error = "roster.json is unreadable";
        return false; // the loader already logged the reason
    }
    const auto rows = document->find("roster");
    if (rows == document->end() || !rows->is_array() || rows->empty()) {
        error = "roster.json needs a nonempty 'roster' array";
        LOG_ERROR("meta content: roster.json must carry a nonempty roster array.");
        return false;
    }
    try {
        for (const auto& row : *rows) {
            FMetaRosterRow entry;
            entry.id = row.at("id").get<std::string>();
            entry.name = row.at("name").get<std::string>();
            if (const auto trait = row.find("trait"); trait != row.end()) entry.trait = trait->get<std::string>();
            if (const auto medals = row.find("medals"); medals != row.end()) {
                if (!medals->is_number_integer() || medals->get<std::int64_t>() < 0) {
                    error = "roster row '" + entry.id + "' has an invalid medal count";
                    LOG_ERROR("meta content: {}", error);
                    return false;
                }
                entry.medals = medals->get<int>();
            }
            if (const auto enabled = row.find("enabled"); enabled != row.end()) entry.enabled = enabled->get<bool>();
            if (const auto locked = row.find("locked"); locked != row.end()) entry.locked = locked->get<bool>();
            // Identity rows are either enabled or locked — never both, never neither (the save schema
            // mirrors this shape).
            if (entry.id.empty() || entry.name.empty() || entry.enabled == entry.locked) {
                error = "roster row '" + entry.id + "' is invalid";
                LOG_ERROR("meta content: roster.json row '{}' needs id/name and exactly one of enabled/locked.", entry.id);
                return false;
            }
            if (std::ranges::find(catalog.roster, entry.id, &FMetaRosterRow::id) != catalog.roster.end()) {
                error = "roster repeats '" + entry.id + "'";
                LOG_ERROR("meta content: roster.json repeats id '{}'.", entry.id);
                return false;
            }
            catalog.roster.push_back(std::move(entry));
        }
    } catch (const nlohmann::json::exception& exception) {
        error = std::string("roster.json is invalid: ") + exception.what();
        LOG_ERROR("meta content: roster.json is unusable: {}", exception.what());
        return false;
    }
    return true;
}

bool LoadUnlockCatalog(const FAssetManager& assets, FMetaCatalog& catalog, std::string& error) {
    const auto path = assets.resolvePath("data/unlocks");
    if (!path) {
        error = "unlocks.json is missing";
        LOG_ERROR("meta content: unlocks.json is missing; the unlock catalog is required for boot.");
        return false;
    }
    const auto document = FJsonLoader::Load(*path, 1);
    if (!document) {
        error = "unlocks.json is unreadable";
        return false; // the loader already logged the reason
    }
    const auto rows = document->find("unlocks");
    if (rows == document->end() || !rows->is_array() || rows->empty()) {
        error = "unlocks.json needs a nonempty 'unlocks' array";
        LOG_ERROR("meta content: unlocks.json must carry a nonempty unlocks array.");
        return false;
    }
    try {
        for (const auto& row : *rows) {
            FMetaUnlockEntry entry;
            entry.id = row.at("id").get<std::string>();
            entry.kind = row.at("kind").get<std::string>();
            entry.cost = row.at("cost").get<int>();
            entry.sealGate = row.at("seal_gate").get<int>();
            if (entry.id.empty() || !IsKnownUnlockKind(entry.kind) || !UnlockIdMatchesKind(entry.id, entry.kind) ||
                entry.cost < 0 || entry.sealGate < 0) {
                error = "unlock '" + entry.id + "' is invalid";
                LOG_ERROR("meta content: unlocks.json row '{}' is invalid (kind, id namespace, cost/seal_gate).", entry.id);
                return false;
            }
            if (std::ranges::any_of(catalog.unlocks, [&](const FMetaUnlockEntry& other) { return other.id == entry.id; })) {
                error = "unlocks repeat '" + entry.id + "'";
                LOG_ERROR("meta content: unlocks.json repeats id '{}'.", entry.id);
                return false;
            }
            catalog.unlocks.push_back(std::move(entry));
        }
    } catch (const nlohmann::json::exception& exception) {
        error = std::string("unlocks.json is invalid: ") + exception.what();
        LOG_ERROR("meta content: unlocks.json is unusable: {}", exception.what());
        return false;
    }
    return true;
}

bool LoadMetaCatalog(const FAssetManager& assets, FMetaCatalog& catalog, std::string& error) {
    return LoadRosterCatalog(assets, catalog, error) && LoadUnlockCatalog(assets, catalog, error);
}

/// Defaults with the shipped roster copied in: a fresh session starts with the four enabled members and
/// the four locked stubs.
FMetaSave FreshMetaSave(const FMetaCatalog& catalog) {
    FMetaSave save;
    save.roster = catalog.roster;
    return save;
}

/// The save↔catalog cross-check of the declared validation set: unlocks ⊆ unlocks.json, roster id ∈
/// roster.json. A save failing it is handled by the corruption policy (backup + defaults) — the catalogs
/// are the authority on which ids exist.
bool CatalogAccepts(const FMetaCatalog& catalog, const FMetaSave& save, std::string& problem) {
    for (const auto& id : save.unlocks) {
        const bool known = std::ranges::any_of(catalog.unlocks,
                                               [&](const FMetaUnlockEntry& entry) { return entry.id == id; });
        if (!known) {
            problem = "unlock '" + id + "' is not in unlocks.json";
            return false;
        }
    }
    for (const auto& row : save.roster) {
        const bool known = std::ranges::any_of(catalog.roster,
                                               [&](const FMetaRosterRow& entry) { return entry.id == row.id; });
        if (!known) {
            problem = "roster '" + row.id + "' is not in roster.json";
            return false;
        }
    }
    return true;
}

/// Forward compatibility of the roster: a loaded save keeps only the rows it was written with, so a member
/// added to roster.json later (WP-23) would stay hidden from an existing save. Append every catalog row the
/// save lacks (catalog order, catalog defaults); existing rows (name, medals, flags) are never touched.
/// Not corruption — no backup. Returns true when rows were added (the caller persists once).
bool MergeNewCatalogRoster(const FMetaCatalog& catalog, FMetaSave& save) {
    bool changed = false;
    for (const auto& row : catalog.roster) {
        if (std::ranges::find(save.roster, row.id, &FMetaRosterRow::id) != save.roster.end()) continue;
        save.roster.push_back(row);
        LOG_INFO("[meta] roster row '{}' added to the existing save (new in roster.json).", row.id);
        changed = true;
    }
    return changed;
}

/// WP-20 P20-3 / R8: fill `ladder[rank].checkpoint` from checkpoint.json while meta_save.json is absent.
/// The import is idempotent (max) and NEVER modifies checkpoint.json — the run start depth stays
/// checkpoint.json's job until WP-18 switches the consumption. best_dungeon moves with it so the title
/// line is right for existing players. Returns true when a checkpoint was importable (whether or not the
/// value changed), i.e. the caller should persist the fresh file.
bool ImportCheckpoint(entt::registry& registry, FMetaSave& save) {
    const auto* checkpoint = registry.ctx().find<FDungeonCheckpoint>();
    if (checkpoint == nullptr || !checkpoint->error.empty()) return false;
    const int cleared = std::clamp(checkpoint->cleared, 0, kLadderCheckpointCap);
    auto& lane = save.ladder[std::to_string(kDefaultLadderRank)];
    lane.checkpoint = std::max(lane.checkpoint, cleared);
    lane.bestDungeon = std::max(lane.bestDungeon, lane.checkpoint);
    LOG_INFO("[meta] checkpoint import: ladder {} checkpoint = {} cleared dungeon(s) (checkpoint.json preserved).",
             kDefaultLadderRank, lane.checkpoint);
    return true;
}

enum class ECorruptOutcome { Reset, ReadOnly };

/// The corruption policy (R7): rename the unreadable file to `meta_save.json.corrupt-<unix ts>` and run on
/// defaults — the original is preserved for diagnosis. A failed rename keeps the file and makes the
/// session read-only, so a later grant can never overwrite it.
ECorruptOutcome BackupCorruptMeta(const std::filesystem::path& path, std::string& reason) {
    if (!FileExists(path)) return ECorruptOutcome::Reset; // nothing to back up (removed under us)
    const std::int64_t stamp =
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    for (int attempt = 0; attempt < 10; ++attempt) {
        std::filesystem::path backup = path;
        backup += ".corrupt-" + std::to_string(stamp + attempt);
        std::error_code code;
        std::filesystem::rename(path, backup, code);
        if (!code) {
            LOG_WARN("meta save was unreadable; backed up to '{}' and reset to defaults.", backup.string());
            return ECorruptOutcome::Reset;
        }
        if (!FileExists(backup)) {
            reason = "the unreadable meta save could not be backed up";
            return ECorruptOutcome::ReadOnly;
        }
        // The backup name is taken (two resets in one second): retry with the next stamp.
    }
    reason = "the unreadable meta save could not be backed up";
    return ECorruptOutcome::ReadOnly;
}

/// Writes the store once for one change (R5: exactly one save per listener event that changed something;
/// never for a no-op event). Never writes for a disabled session.
bool SaveMetaProgress(entt::registry& registry) {
    auto* store = registry.ctx().find<FMetaProgressStore>();
    if (store == nullptr || store->disabled || store->path.empty()) return false;
    store->save.schemaVersion = kMetaSaveVersion;
    store->save.savedAt = MetaUtcTimestamp();
    std::string error;
    if (!WriteSaveFile(store->path, MetaSaveToJson(store->save), error)) {
        if (!store->saveWarned) {
            store->saveWarned = true;
            LOG_WARN("meta progress could not be saved: {}", error);
        }
        return false;
    }
    store->saveWarned = false;
    return true;
}

/// The milestone derivation (WP-20 R1/R3): the dungeon position comes from the EVENT ITSELF,
/// `PositionOf(event.depth - 1)` — never from run.cleared. At BossDefeated time cleared is still depth-1
/// and at DungeonCleared time it is depth, so a cleared-derived key would split one dungeon's `#boss` and
/// `#clear` across two different indices.
struct FMetaMilestone {
    std::string key;
    FRunSequence::FPosition position{};
    const FRunRoomSlot* slot{nullptr};
    const FRunSequence* sequence{nullptr};
};

std::optional<FMetaMilestone> MilestoneFor(
    entt::registry& registry, FMetaProgressStore& store, const FRunEvent& event, std::string_view suffix) {
    if (event.depth <= 0) {
        if (!store.warnedZeroDepth) {
            store.warnedZeroDepth = true;
            LOG_WARN("[meta] a milestone event carried no run depth; the grant is skipped.");
        }
        return std::nullopt;
    }
    const auto* sequence = FindRunSequence(registry);
    if (sequence == nullptr || sequence->themes.empty()) {
        if (!store.warnedNoSequence) {
            store.warnedNoSequence = true;
            LOG_WARN("[meta] the dungeon sequence is missing; milestone grants are skipped this session.");
        }
        return std::nullopt;
    }
    FMetaMilestone milestone;
    milestone.position = sequence->PositionOf(event.depth - 1);
    if (milestone.position.theme >= sequence->themes.size()) return std::nullopt; // defensive; PositionOf clamps
    // The key deliberately carries NO cycle: a re-clear of the same dungeon in a later cycle grants 0
    // (D-22 "반복 클리어 0" — 파밍 차단; Hades' "once per (Heat level, boss)" reading: our rank is the
    // Heat analogue, a cycle is not a new milestone).
    milestone.key = sequence->themes[milestone.position.theme].id + "/" +
        std::to_string(milestone.position.dungeonInTheme) + std::string(suffix);
    milestone.slot = sequence->BattleSlotAt(milestone.position, event.room);
    milestone.sequence = sequence;
    return milestone;
}

bool HasGranted(const FMetaLadderRank& lane, std::string_view key) {
    return std::ranges::find(lane.granted, key) != lane.granted.end();
}

/// True when this breakthrough counts as a GRAND milestone (D-19: a milestone record, NOT a run end).
/// The event's room resolves the slot; when the sequence no longer has that room (content changed under a
/// save) the fallback asks whether the dungeon has a final slot at all (WP-20 R3) — mid is never promoted.
bool IsFinalBossSlot(const FMetaMilestone& milestone) {
    if (milestone.slot != nullptr) return milestone.slot->boss == "final";
    for (const auto& slot : milestone.sequence->DungeonAt(milestone.position).rooms) {
        if (slot.boss == "final") return true;
    }
    return false;
}

/// DungeonCleared: +1 seal for the first clear of (rank, dungeon position) and the clear record. The
/// statistics count every clear (cycles included); best_depth/cycle_best and the ladder maxima never
/// regress (LadderRecordsBest).
bool GrantDungeonClear(FMetaProgressStore& store, const FRunEvent& event, const FMetaMilestone& milestone) {
    auto& lane = store.save.ladder[std::to_string(kDefaultLadderRank)];
    if (!HasGranted(lane, milestone.key)) {
        lane.granted.push_back(milestone.key);
        store.save.seals += 1;
        LOG_INFO("[meta] +1 seal: dungeon clear {} (D-14/D-22: first clear only).", milestone.key);
    }
    store.save.stats.clears += 1;
    store.save.stats.bestDepth = std::max(store.save.stats.bestDepth, event.depth);
    store.save.stats.cycleBest = std::max(store.save.stats.cycleBest, milestone.position.cycle);
    // Same value the real producer stores in checkpoint.json: min(cleared, cap) — see kLadderCheckpointCap.
    const int cleared = std::min(event.depth, kLadderCheckpointCap);
    lane.bestDungeon = std::max(lane.bestDungeon, cleared);
    lane.checkpoint = std::max(lane.checkpoint, cleared);
    return true;
}

/// BossDefeated (the WP-15 cross contract; tests append it directly — standalone green): +2 seals for the
/// first breakthrough of (rank, dungeon position). A final slot also records the grand milestone:
/// stats.grand counts every final defeat (cycles included), ladder[rank].grand only the first grant.
bool GrantBossDefeat(FMetaProgressStore& store, const FMetaMilestone& milestone) {
    auto& lane = store.save.ladder[std::to_string(kDefaultLadderRank)];
    const bool first = !HasGranted(lane, milestone.key);
    if (first) {
        lane.granted.push_back(milestone.key);
        store.save.seals += 2;
        LOG_INFO("[meta] +2 seals: boss breakthrough {} (D-14/D-22: first clear only).", milestone.key);
    }
    const bool final = IsFinalBossSlot(milestone);
    if (final) {
        store.save.stats.grand += 1;
        if (first) lane.grand += 1;
        LOG_INFO("[meta] grand milestone: final boss {} (D-19: milestone, not a run end).", milestone.key);
    }
    return first || final;
}

/// The FRunEventLog subscription (registered once by InitializeMetaProgress). It only reacts to the three
/// kinds that carry meta state; RoomCleared/RewardPicked/CampSpent/EventChosen change nothing and never
/// save. It never appends events (a re-entrant append is dropped by AppendRunEvent) and never assumes
/// delivery order relative to other listeners (WP-19a's run log is independent).
void HandleMetaRunEvent(entt::registry& registry, const FRunEvent& event) {
    auto* store = registry.ctx().find<FMetaProgressStore>();
    if (store == nullptr) return; // after ShutdownMetaProgress: the (never removed) listener is inert
    if (store->disabled) {
        if (event.kind == ERunEventKind::DungeonCleared || event.kind == ERunEventKind::BossDefeated) {
            LOG_INFO("[meta] grant skipped — meta is disabled ({}).", store->reason);
        }
        return;
    }
    bool changed = false;
    switch (event.kind) {
    case ERunEventKind::DungeonCleared: {
        if (const auto milestone = MilestoneFor(registry, *store, event, "#clear")) {
            changed = GrantDungeonClear(*store, event, *milestone);
        }
        break;
    }
    case ERunEventKind::BossDefeated: {
        if (const auto milestone = MilestoneFor(registry, *store, event, "#boss")) {
            changed = GrantBossDefeat(*store, *milestone);
        }
        break;
    }
    case ERunEventKind::RunEnded:
        // D-19/D-22: failure grants nothing; the run counter is a statistic, not a grant.
        store->save.stats.runs += 1;
        changed = true;
        break;
    default: break;
    }
    if (changed) SaveMetaProgress(registry);
}

} // namespace

bool InitializeMetaProgress(entt::registry& registry, std::string path, FMetaPrefDirProvider prefPath) {
    const auto* assets = registry.ctx().find<FAssetManager>();
    if (assets == nullptr) {
        LOG_ERROR("meta progress needs the asset boundary: FAssetManager is missing from the registry context.");
        return false;
    }
    // Shipped content first: a missing or invalid roster.json / unlocks.json stops the boot regardless of
    // the storage state (FRewards.h:50 precedent — "a missing file or a validation failure returns false
    // and the boot stops"). Save corruption (reset allowed, backup first) and content corruption (boot
    // failure) are different grades.
    FMetaProgressStore store;
    std::string error;
    if (!LoadMetaCatalog(*assets, store.catalog, error)) {
        registry.ctx().erase<FMetaProgressStore>();
        LOG_ERROR("meta content is unusable ({}); boot stopped (see the meta content errors above).", error);
        return false;
    }
    if (!path.empty()) {
        // Injected file path (tests/tools) outranks every resolution rule.
        const std::filesystem::path file(path);
        std::error_code code;
        if (file.has_parent_path()) std::filesystem::create_directories(file.parent_path(), code);
        if (code) {
            store.disabled = true;
            store.reason = "the injected meta path is unusable";
            LOG_WARN("meta progress is off ({}); nothing will be persisted this session.", store.reason);
        } else {
            store.path = file;
        }
    } else {
        const auto overrideDir = SaveDirOverride();
        const FMetaPrefDirProvider prefDir = prefPath ? std::move(prefPath) : FMetaPrefDirProvider(PreferenceDirectory);
        if (const auto resolved = ResolveMetaSavePath(overrideDir, CheckpointPath(registry), prefDir)) {
            store.path = *resolved;
        }
        if (store.path.empty()) {
            store.disabled = true;
            store.reason = overrideDir.has_value() ? "the GAME01P_SAVE_DIR override is unusable"
                                                   : "no meta storage directory is available";
            LOG_WARN("meta progress is off ({}); nothing will be persisted this session.", store.reason);
        }
    }

    FMetaSave save = FreshMetaSave(store.catalog);
    bool saveAfterLoad = false;
    if (!store.disabled) {
        if (FileExists(store.path)) {
            const auto document = ReadSaveFile(store.path);
            bool corrupt = false;
            if (!document) {
                LOG_WARN("meta save '{}' is unreadable ({}).", store.path.string(), document.error());
                corrupt = true;
            } else if (FutureMetaVersion(*document)) {
                // A newer file is never read, never reset and never overwritten: later grants would write
                // through SaveMetaProgress, so the whole session must be read-only (R5). "Continue on
                // defaults" would actually commit the overwrite this policy exists to prevent.
                store.disabled = true;
                store.reason = "the meta save was written by a newer version";
                LOG_WARN("meta save '{}' is from a newer version; it is left untouched and meta is read-only this session.",
                         store.path.string());
            } else if (auto parsed = MetaSaveFromJson(*document); !parsed) {
                LOG_WARN("meta save '{}' is invalid ({}).", store.path.string(), parsed.error());
                corrupt = true;
            } else if (!CatalogAccepts(store.catalog, *parsed, error)) {
                LOG_WARN("meta save '{}' does not match the shipped catalogs ({}).", store.path.string(), error);
                corrupt = true;
            } else {
                save = std::move(*parsed);
                saveAfterLoad = MergeNewCatalogRoster(store.catalog, save);
            }
            if (corrupt) {
                if (BackupCorruptMeta(store.path, store.reason) == ECorruptOutcome::Reset) {
                    // The backup succeeded: run on defaults, recover the checkpoint progress and write
                    // the fresh file right away. The backup keeps the original for diagnosis.
                    save = FreshMetaSave(store.catalog);
                    ImportCheckpoint(registry, save);
                    saveAfterLoad = true;
                } else {
                    store.disabled = true;
                    LOG_WARN("meta progress is off ({}); nothing will be persisted this session.", store.reason);
                }
            }
        } else {
            // First run (or the user deleted the file): the checkpoint import is the recovery path, and
            // the fresh file is written right after it (WP-20 P20-3). checkpoint.json itself is preserved.
            saveAfterLoad = ImportCheckpoint(registry, save);
        }
    }

    store.save = std::move(save);
    registry.ctx().insert_or_assign<FMetaProgressStore>(std::move(store));

    // Register exactly once per registry, and keep the marker alive across ShutdownMetaProgress (R9).
    auto* wiring = registry.ctx().find<FMetaProgressWiring>();
    if (wiring == nullptr) wiring = &registry.ctx().emplace<FMetaProgressWiring>();
    if (!wiring->listenerRegistered) {
        wiring->listenerRegistered = true;
        RegisterRunEventListener(registry, [&registry](const FRunEvent& event) { HandleMetaRunEvent(registry, event); });
    }

    if (saveAfterLoad && !registry.ctx().get<FMetaProgressStore>().disabled) {
        SaveMetaProgress(registry);
    }
    const auto& stored = registry.ctx().get<FMetaProgressStore>();
    if (stored.disabled) {
        LOG_INFO("meta progress initialized in read-only mode: {}", stored.reason);
    } else {
        LOG_INFO("meta progress ready: '{}' (seals {}).", stored.path.string(), stored.save.seals);
    }
    return true;
}

void ShutdownMetaProgress(entt::registry& registry) {
    // A no-op without an initialized store (the forest/verify shutdown paths). The wiring marker survives
    // on purpose: a later InitializeMetaProgress does not register a second listener.
    registry.ctx().erase<FMetaProgressStore>();
}

int MetaSeals(entt::registry& registry) {
    const auto* store = registry.ctx().find<FMetaProgressStore>();
    return store != nullptr ? store->save.seals : 0;
}

bool MetaDisabled(entt::registry& registry) {
    const auto* store = registry.ctx().find<FMetaProgressStore>();
    return store == nullptr || store->disabled;
}

void GrantSeals(entt::registry& registry, int amount, std::string_view reason) {
    auto* store = registry.ctx().find<FMetaProgressStore>();
    if (store == nullptr) {
        LOG_WARN("[meta] seals were granted before InitializeMetaProgress ('{}'); nothing was recorded.", reason);
        return;
    }
    if (store->disabled) {
        LOG_INFO("[meta] {} seal(s) logged only — {} ({}).", amount, store->reason, reason);
        return;
    }
    if (amount == 0) return;
    store->save.seals = std::max(0, store->save.seals + amount);
    LOG_INFO("[meta] seals {:+d} ({}). Now {}.", amount, reason, store->save.seals);
    SaveMetaProgress(registry);
}

void GrantMetaUnlock(entt::registry& registry, std::string_view unlockId) {
    auto* store = registry.ctx().find<FMetaProgressStore>();
    if (store == nullptr) {
        LOG_WARN("[meta] unlock '{}' was granted before InitializeMetaProgress; nothing was recorded.", unlockId);
        return;
    }
    if (store->disabled) {
        LOG_INFO("[meta] unlock '{}' logged only — {}.", unlockId, store->reason);
        return;
    }
    // An id outside the catalog would make the next load fail its cross-check (unlocks ⊆ unlocks.json):
    // refuse it at the source.
    if (!std::ranges::any_of(store->catalog.unlocks,
                             [&](const FMetaUnlockEntry& entry) { return entry.id == unlockId; })) {
        LOG_WARN("[meta] unknown unlock id '{}' ignored (it is not in unlocks.json).", unlockId);
        return;
    }
    if (std::ranges::find(store->save.unlocks, unlockId) != store->save.unlocks.end()) return; // idempotent
    store->save.unlocks.emplace_back(unlockId);
    LOG_INFO("[meta] unlock granted: {}", unlockId);
    SaveMetaProgress(registry);
}

bool HasMetaUnlock(entt::registry& registry, std::string_view unlockId) {
    const auto* store = registry.ctx().find<FMetaProgressStore>();
    if (store == nullptr) return false;
    return std::ranges::find(store->save.unlocks, unlockId) != store->save.unlocks.end();
}

std::string MetaSummaryLine(entt::registry& registry) {
    const auto* store = registry.ctx().find<FMetaProgressStore>();
    if (store == nullptr) return {}; // never initialized: the title draws nothing
    if (store->disabled) return "meta disabled";
    int best = 0;
    if (const auto lane = store->save.ladder.find(std::to_string(kDefaultLadderRank));
        lane != store->save.ladder.end()) {
        best = lane->second.bestDungeon;
    }
    return "Seals " + std::to_string(store->save.seals) + " · Best dungeon " + std::to_string(best) +
        " · Clears " + std::to_string(store->save.stats.clears);
}

const FMetaSave* FindMetaSave(const entt::registry& registry) {
    const auto* store = registry.ctx().find<FMetaProgressStore>();
    return store != nullptr ? &store->save : nullptr;
}

std::filesystem::path MetaSavePath(const entt::registry& registry) {
    const auto* store = registry.ctx().find<FMetaProgressStore>();
    return store != nullptr ? store->path : std::filesystem::path{};
}

std::optional<std::filesystem::path> ResolveMetaSavePath(const std::optional<std::filesystem::path>& overrideDir,
                                                         std::string_view checkpointPath,
                                                         const FMetaPrefDirProvider& prefPath) {
    if (overrideDir.has_value()) {
        std::error_code code;
        std::filesystem::create_directories(*overrideDir, code);
        if (code) {
            // Never fall back to the preference directory: that would be the user's real save location.
            return std::nullopt;
        }
        return *overrideDir / std::filesystem::path(kMetaSaveFileName);
    }
    if (!checkpointPath.empty()) {
        const auto parent = std::filesystem::path(checkpointPath).parent_path();
        if (!parent.empty()) return parent / std::filesystem::path(kMetaSaveFileName);
    }
    if (prefPath) {
        if (const auto directory = prefPath()) return *directory / std::filesystem::path(kMetaSaveFileName);
    }
    return std::nullopt;
}

} // namespace game
