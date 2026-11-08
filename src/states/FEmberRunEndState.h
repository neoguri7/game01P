#pragma once

#include <string>
#include <string_view>
#include <vector>

#include <entt/entt.hpp>

#include "FBaseState.h"
#include "gameplay/dungeon/FDungeonRun.h"
#include "gameplay/dungeon/FRunFlow.h"
#include "gameplay/dungeon/FRunLog.h"

/// Catalog id owned by this state (WP-02b). The runEnd modal is a real state: it draws the defeat view
/// and offers retry (back to prepare, from the checkpoint) or leave (to the title).
inline constexpr std::string_view kEmberRunEndId = game::kRunModalRunEnd;

/// [WP-19b] Rollback lever (WP-19b risk/rollback): false hides the decisive-scenes section only — the
/// three-part debrief (gained / lost-vs-kept / next run), the fatal line, the rooms list and the buttons
/// are unaffected.
inline constexpr bool kRunEndShowScenes = true;

/// The run-end text, split so the screen lays the parts out as sections. `metrics` is the report block,
/// `rooms` is one line per room (the "Rooms" heading is the screen's), `fatal` is the one fatal-intent
/// statement — the WP-19a E8 text, byte-pinned by tests/features/run_log_tests.cpp. WP-19b adds the
/// debrief blocks below.
struct FRunEndSummary {
    std::vector<std::string> metrics;
    std::vector<std::string> rooms;
    std::vector<std::string> fatal;
    std::vector<std::string> scenes;    // [WP-19b] RunSceneLines of every BuildRunScenes scene, in order
    std::vector<std::string> cycle;     // [WP-19b] "Cycle {N} - deepest depth {M}" when the last run_end's cycle > 0
    std::vector<std::string> friction;  // [WP-19b] "Time to first battle: {s:.1f} s (dev metric)" when recorded
    bool fatalInScene{false};           // [WP-19b] a BuildRunScenes scene carries ERunSceneRule::FatalIntent
};

/// The run-end text (WP-19a E8, extended by WP-19b): the report metrics, one line per room (with its offer
/// pick and that depth's camp spend), the one fatal enemy intent as facts only, then the decisive scenes,
/// the cycle and the friction lines. This is exactly `BuildRunEndSummary` joined with blank lines, so
/// text-only consumers and tests keep one canonical text. Pure over the log so tests can check the text
/// without ImGui.
[[nodiscard]] std::vector<std::string> BuildRunEndSummaryLines(const game::FRunLog& log);
[[nodiscard]] FRunEndSummary BuildRunEndSummary(const game::FRunLog& log);

/// [WP-19b] A scene as text: one heading line "{title} - Depth {d} room {r}" then one "  {label}: {value}"
/// line per fact. Pure.
[[nodiscard]] std::vector<std::string> RunSceneLines(const game::FRunScene& scene);

/// [WP-19b] ③ "What changes next run" inputs, read from the registry (meta + checkpoint), never from the log.
struct FNextRunDeltas {
    bool metaDisabled{true};                    // MetaDisabled(registry) or no meta save
    int seals{0};                               // MetaSeals
    std::vector<std::string> unlockCandidates;  // Purchasable BuildUnlockOffers rows -> "{id} ({cost} seals)"
    int ladderCheckpoint{0};                    // MetaLadderCheckpoint(registry, 1)
    int checkpointCleared{0};                   // FDungeonCheckpoint::cleared (the run-start source today)
    int grandCount{0};                          // MetaGrandCount
    int rosterSize{0};                          // unlocked roster rows (locked rows excluded; 0 when no meta save)
    int ownedUnlocks{0};                        // FMetaSave::unlocks.size() (0 when no meta save)
};

/// [WP-19b] The deltas as text. Pure. Meta disabled -> the checkpoint line plus exactly "Unlocks and seals
/// are shown when the unlock system is active." and nothing seal/unlock/ladder/achievement related;
/// otherwise one concrete item per number (seals, unlock candidates, the ladder-1 checkpoint, the D-21
/// start range and the D-22 achievement count).
[[nodiscard]] FNextRunDeltas NextRunDeltas(entt::registry& registry);
[[nodiscard]] std::vector<std::string> NextRunDeltaLines(const FNextRunDeltas& deltas);

/// [WP-19b] ① gained / ② lost-vs-kept, from the run (nullable) + log. Pure — the numbers a kept line needs
/// come from the passed deltas, never from the registry.
struct FRunEndLedger {
    std::vector<std::string> gained;  // ① dungeons cleared (dungeon_clear lines), runes (count + ids),
                                      //   materials (camp spend + the unspent balance — the held facts,
                                      //   never an "earned" total), levels gained (sum of level-1),
                                      //   passive nodes allocated (sum of allocatedNodes)
    std::vector<std::string> lost;    // ② what this expedition loses: runes (count only — ids in ①), the
                                      //   unspent materials, levels and nodes
    std::vector<std::string> kept;    // ② what stays: checkpoint + roster size + owned unlocks
                                      //   (numbers from FNextRunDeltas)
};
[[nodiscard]] FRunEndLedger BuildRunEndLedger(const game::FDungeonRun* run, const game::FRunLog& log,
                                              const FNextRunDeltas& deltas);

/// Run-end screen. The view posts RetryAfterDefeat or LeaveRun; WP-19a replaced the stub presentation, not
/// the contract. It never pops: a run end is a screen, not a modal.
class FEmberRunEndState final : public FBaseState
{
public:
    FStateTransition Update(entt::registry& Registry, float DeltaTime) override;
    void Render(entt::registry& Registry) override;
    [[nodiscard]] std::string_view GetName() const noexcept override { return "RunEnd"; }
    [[nodiscard]] std::span<const std::string_view> RequiredSystems() const noexcept override { return {}; }
};
