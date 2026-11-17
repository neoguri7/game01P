#include "gameplay/dungeon/FRoomGenerator.h"

#include <algorithm>
#include <queue>
#include <set>

namespace game::gameplay {
namespace {

constexpr int kSize = FGeneratedRoom::kSize;
constexpr int kEdgeMargin = 1;  // the outermost ring stays empty so the room reads as an open map
constexpr int kEnemyFirstRow = kRoomEnemyFirstRow;  // the spawn band contract lives in the header
constexpr int kEnemyLastRow = kRoomEnemyLastRow;
constexpr int kClutterFirstRow = 3;
constexpr int kClutterLastRow = 8;

class FRandom {
public:
    explicit FRandom(std::uint32_t seed) : state_(seed) {}

    std::uint32_t next() {
        state_ += 0x9E3779B9u;
        std::uint32_t z = state_;
        z = (z ^ (z >> 16)) * 0x21F0AAADu;
        z = (z ^ (z >> 15)) * 0x735A2D97u;
        return z ^ (z >> 15);
    }
    int below(int bound) { return static_cast<int>(next() % static_cast<std::uint32_t>(bound)); }
    int in(FIntRange range) { return range.min + below(range.max - range.min + 1); }

private:
    std::uint32_t state_;
};

struct FCell {
    int x;
    int y;
    bool operator==(const FCell&) const = default;
};

using FBoard = std::array<std::array<char, kSize>, kSize>;

bool InClutterBand(int x, int y) {
    return x >= kEdgeMargin && x <= kSize - 1 - kEdgeMargin && y >= kClutterFirstRow && y <= kClutterLastRow;
}

bool AllConnected(const FBoard& board, std::span<const FCell> units) {
    if (units.empty()) return true;
    std::array<std::array<bool, kSize>, kSize> seen{};
    std::queue<FCell> open;
    open.push(units.front());
    seen[units.front().y][units.front().x] = true;
    while (!open.empty()) {
        const auto cell = open.front();
        open.pop();
        for (const auto [dx, dy] : {std::pair{1, 0}, std::pair{-1, 0}, std::pair{0, 1}, std::pair{0, -1}}) {
            const int nx = cell.x + dx;
            const int ny = cell.y + dy;
            if (nx < 0 || ny < 0 || nx >= kSize || ny >= kSize || seen[ny][nx] || board[ny][nx] == '#') continue;
            seen[ny][nx] = true;
            open.push({nx, ny});
        }
    }
    return std::ranges::all_of(units, [&](FCell unit) { return seen[unit.y][unit.x]; });
}

/// Deterministic fallback for a heavy layout that (against expectation) split the room: the wall cells are
/// dismantled piece by piece in the given order until every unit is connected again. With the empty outer
/// ring the current shapes never fire it; it exists so no family can ever return a broken room.
void DropWallsUntilConnected(FBoard& board, std::span<const FCell> walls, std::span<const FCell> units) {
    if (AllConnected(board, units)) return;
    for (const auto cell : walls) {
        board[cell.y][cell.x] = '.';
        if (AllConnected(board, units)) return;
    }
}

std::vector<FCell> FeatureShape(FRandom& random, FCell anchor) {
    switch (random.below(5)) {
    case 0: return {anchor};
    case 1: return {anchor, {anchor.x + 1, anchor.y}};
    case 2: return {anchor, {anchor.x + 1, anchor.y}, {anchor.x + 2, anchor.y}};
    case 3: return {anchor, {anchor.x, anchor.y + 1}};
    default: return {anchor, {anchor.x + 1, anchor.y}, {anchor.x, anchor.y + 1}};
    }
}

const FEnemyChoice& PickEnemy(FRandom& random, const std::vector<FEnemyChoice>& pool) {
    int total = 0;
    for (const auto& choice : pool) total += std::max(1, choice.weight);
    int roll = random.below(total);
    for (const auto& choice : pool) {
        roll -= std::max(1, choice.weight);
        if (roll < 0) return choice;
    }
    return pool.back();
}

std::uint32_t Mix(std::uint32_t seed, int depth, int room) {
    FRandom mixer(seed ^ (static_cast<std::uint32_t>(depth) * 0x85EBCA6Bu) ^
                  (static_cast<std::uint32_t>(room) * 0xC2B2AE35u));
    mixer.next();
    return mixer.next();
}

// --- shared room scaffolding (WP-08b: the family layouts reuse the classic head) -----------------

/// The spawn band slots, one per cell (rows kEnemyFirstRow..kEnemyLastRow, edge margin respected).
std::vector<FCell> BandSlots() {
    std::vector<FCell> slots;
    for (int y = kEnemyFirstRow; y <= kEnemyLastRow; ++y) {
        for (int x = kEdgeMargin; x < kSize - kEdgeMargin; ++x) slots.push_back({x, y});
    }
    return slots;
}

void ShuffleSlots(FRandom& random, std::vector<FCell>& slots) {
    for (std::size_t i = slots.size(); i > 1; --i) {
        std::swap(slots[i - 1], slots[static_cast<std::size_t>(random.below(static_cast<int>(i)))]);
    }
}

/// The weighted pool enemy `index` draws from: boss rooms and legacy configs keep the pre-WP-14 `enemies`
/// pool; an encounter composition takes role slot `index` first, then `fill`, and defensively the last role
/// slot's pool when the fill is missing (the loader forbids that case). Placement itself stays one loop below.
const std::vector<FEnemyChoice>& EnemyPoolFor(const FRoomGenConfig& config, bool boss, int index) {
    if (boss || config.roleSlots.empty()) return config.enemies;
    if (index < static_cast<int>(config.roleSlots.size())) {
        return config.roleSlots[static_cast<std::size_t>(index)].units;
    }
    if (!config.fill.empty()) return config.fill;
    return config.roleSlots.back().units;
}

/// Enemy placement on a prepared slot list: the slot order is the family's formation and this is the SINGLE
/// placement rule (P14-2 — role picks, extras and the leader prefix never grow a second algorithm). Boss
/// rooms keep the classic lead: the boss unit, then `config.enemies` picks; roleSlots, extras and the leader
/// prefix are ignored. A non-boss room draws the base count first (`random.in(config.enemyCount)` — the same
/// first draw as the pre-WP-14 generator): with role slots, enemy i is picked from roleSlots[i] and the rest
/// of the count from `fill`; without role slots the weighted `config.enemies` pool is drawn exactly as
/// before, so that path stays byte-identical. The extras (`request.extraEnemyCount`) then take the NEXT slots
/// of the same list with NO extra random draws; the total is min(count + extras, slots.size()), so the base
/// composition is seated first and the extras shrink when the family runs out of spawn slots (never a failed
/// room — W10 §5-4). The leader prefix renames enemy.1 (P14-4). Every family seats enemies before its wall
/// pass, so "walls never disconnect any unit" covers the extras too.
void PlaceEnemies(FRandom& random, const FRoomGenConfig& config, const FRoomRequest& request,
                  std::span<const FCell> slots, std::vector<FCell>& units, FGeneratedRoom& room) {
    const bool boss = request.boss;
    int enemyCount = boss ? 3 : random.in(config.enemyCount);
    enemyCount = std::min(enemyCount, static_cast<int>(slots.size()));
    for (int i = 0; i < enemyCount; ++i) {
        FGeneratedEnemy enemy;
        if (boss && i == 0) {
            enemy.unit = config.bossUnit;
            enemy.displayName = config.bossName;
        } else {
            const auto& choice = PickEnemy(random, EnemyPoolFor(config, boss, i));
            enemy.unit = choice.unit;
            enemy.displayName = choice.displayName;
        }
        enemy.x = slots[static_cast<std::size_t>(i)].x;
        enemy.y = slots[static_cast<std::size_t>(i)].y;
        units.push_back({enemy.x, enemy.y});
        room.enemies.push_back(std::move(enemy));
    }
    if (!boss && request.extraEnemyCount > 0) {
        const int total = std::min(enemyCount + request.extraEnemyCount, static_cast<int>(slots.size()));
        for (int i = enemyCount; i < total; ++i) {
            const FCell slot = slots[static_cast<std::size_t>(i)];
            units.push_back(slot);
            room.enemies.push_back({request.extraEnemyUnit, request.extraEnemyName, slot.x, slot.y});
        }
    }
    if (!boss && !request.leaderNamePrefix.empty() && !room.enemies.empty()) {
        room.enemies.front().displayName = request.leaderNamePrefix + room.enemies.front().displayName;
    }
}

/// The classic head: name, empty board, the party, and the shuffled-band formation.
FGeneratedRoom BeginClassicRoom(FRandom& random, const FRoomGenConfig& config, const FRoomRequest& request,
                                FBoard& board, std::vector<FCell>& units) {
    FGeneratedRoom room;
    room.name = config.names[static_cast<std::size_t>(random.below(static_cast<int>(config.names.size())))];
    for (auto& row : board) row.fill('.');
    for (const auto& [x, y] : request.partyCells) units.push_back({x, y});
    auto slots = BandSlots();
    ShuffleSlots(random, slots);
    PlaceEnemies(random, config, request, slots, units, room);
    return room;
}

/// The classic scattered traps (the `open` rule), shared by the heavy families.
void PlaceBandTraps(FRandom& random, FBoard& board, std::span<const FCell> units, int budget) {
    const auto occupied = [&](FCell cell) { return std::ranges::find(units, cell) != units.end(); };
    for (int attempt = 0, traps = 0; attempt < 60 && traps < budget; ++attempt) {
        const FCell cell{kEdgeMargin + random.below(kSize - 2 * kEdgeMargin),
                         kClutterFirstRow + random.below(kClutterLastRow - kClutterFirstRow + 1)};
        if (board[cell.y][cell.x] != '.' || occupied(cell)) continue;
        board[cell.y][cell.x] = '^';
        ++traps;
    }
}

void WriteTerrain(FGeneratedRoom& room, const FBoard& board) {
    for (int y = 0; y < kSize; ++y) room.terrain[static_cast<std::size_t>(y)] = std::string(board[y].begin(), board[y].end());
}

// --- per-family generators ----------------------------------------------------------------------

/// `open` (light): the classic algorithm, byte-identical to the pre-WP-08b generator. The draw order here is
/// load-bearing: a golden test pins its output, so edits must keep every draw in place.
FGeneratedRoom GenerateOpen(const FRoomGenConfig& config, const FRoomRequest& request) {
    FRandom random(Mix(request.seed, request.depth, request.room));
    FBoard board;
    std::vector<FCell> units;
    FGeneratedRoom room = BeginClassicRoom(random, config, request, board, units);

    const auto occupied = [&](FCell cell) { return std::ranges::find(units, cell) != units.end(); };

    // Wall clusters: pillars, short walls and corners; a cluster that would split the room is dropped.
    const int budget = random.in(config.obstacleCount);
    int placed = 0;
    for (int attempt = 0; attempt < 80 && placed < budget; ++attempt) {
        const FCell anchor{kEdgeMargin + random.below(kSize - 2 * kEdgeMargin),
                           kClutterFirstRow + random.below(kClutterLastRow - kClutterFirstRow + 1)};
        const auto shape = FeatureShape(random, anchor);
        const bool fits = std::ranges::all_of(shape, [&](FCell cell) {
            return InClutterBand(cell.x, cell.y) && board[cell.y][cell.x] == '.' && !occupied(cell);
        });
        if (!fits || placed + static_cast<int>(shape.size()) > budget) continue;
        for (const auto cell : shape) board[cell.y][cell.x] = '#';
        if (!AllConnected(board, units)) {
            for (const auto cell : shape) board[cell.y][cell.x] = '.';
            continue;
        }
        placed += static_cast<int>(shape.size());
    }

    const int trapBudget = random.in(config.trapCount);
    for (int attempt = 0, traps = 0; attempt < 60 && traps < trapBudget; ++attempt) {
        const FCell cell{kEdgeMargin + random.below(kSize - 2 * kEdgeMargin),
                         kClutterFirstRow + random.below(kClutterLastRow - kClutterFirstRow + 1)};
        if (board[cell.y][cell.x] != '.' || occupied(cell)) continue;
        board[cell.y][cell.x] = '^';
        ++traps;
    }

    WriteTerrain(room, board);
    return room;
}

/// `open_variant` (light): the same wall budget as `open`, but the placement is anchored at the clutter-band
/// corners, the traps sit at the centre and the spawns take the flanks of the band (still inside it).
FGeneratedRoom GenerateOpenVariant(const FRoomGenConfig& config, const FRoomRequest& request) {
    FRandom random(Mix(request.seed, request.depth, request.room));
    FBoard board;
    for (auto& row : board) row.fill('.');
    FGeneratedRoom room;
    room.name = config.names[static_cast<std::size_t>(random.below(static_cast<int>(config.names.size())))];
    std::vector<FCell> units;
    for (const auto& [x, y] : request.partyCells) units.push_back({x, y});

    // Flank spawns: the two outermost columns of each side, shuffled per side, taken alternately. The band
    // rows never move (the spawn-band contract).
    std::vector<FCell> leftFlank;
    std::vector<FCell> rightFlank;
    for (int y = kEnemyFirstRow; y <= kEnemyLastRow; ++y) {
        for (int x = kEdgeMargin; x <= kEdgeMargin + 1; ++x) leftFlank.push_back({x, y});
        for (int x = kSize - 1 - kEdgeMargin; x >= kSize - 2 - kEdgeMargin; --x) rightFlank.push_back({x, y});
    }
    ShuffleSlots(random, leftFlank);
    ShuffleSlots(random, rightFlank);
    std::vector<FCell> flank;
    for (std::size_t i = 0; i < std::max(leftFlank.size(), rightFlank.size()); ++i) {
        if (i < leftFlank.size()) flank.push_back(leftFlank[i]);
        if (i < rightFlank.size()) flank.push_back(rightFlank[i]);
    }
    PlaceEnemies(random, config, request, flank, units, room);

    const auto occupied = [&](FCell cell) { return std::ranges::find(units, cell) != units.end(); };

    // Corner walls: the four corners of the clutter band in a random order; each corner takes a two-cell L
    // (one of two inward orientations) or a single cell, inside the same wall budget as `open`.
    const int budget = random.in(config.obstacleCount);
    std::vector<FCell> corners = {{kEdgeMargin, kClutterFirstRow},
                                  {kSize - 1 - kEdgeMargin, kClutterFirstRow},
                                  {kEdgeMargin, kClutterLastRow},
                                  {kSize - 1 - kEdgeMargin, kClutterLastRow}};
    ShuffleSlots(random, corners);
    int placed = 0;
    for (const FCell corner : corners) {
        if (placed >= budget) break;
        const int inwardX = corner.x == kEdgeMargin ? 1 : -1;
        const int inwardY = corner.y == kClutterFirstRow ? 1 : -1;
        const std::vector<FCell> horizontal{corner, {corner.x + inwardX, corner.y}};
        const std::vector<FCell> vertical{corner, {corner.x, corner.y + inwardY}};
        const bool verticalFirst = random.below(2) == 1;
        const std::vector<FCell> single{corner};
        for (const auto* shape : {verticalFirst ? &vertical : &horizontal, verticalFirst ? &horizontal : &vertical, &single}) {
            if (placed + static_cast<int>(shape->size()) > budget) continue;
            const bool fits = std::ranges::all_of(*shape, [&](FCell cell) {
                return InClutterBand(cell.x, cell.y) && board[cell.y][cell.x] == '.' && !occupied(cell);
            });
            if (!fits) continue;
            for (const auto cell : *shape) board[cell.y][cell.x] = '#';
            if (!AllConnected(board, units)) {
                for (const auto cell : *shape) board[cell.y][cell.x] = '.';
                continue;
            }
            placed += static_cast<int>(shape->size());
            break;
        }
    }

    // Centre traps: the middle 4x4 of the band, drawn like the classic scatter.
    const int trapBudget = random.in(config.trapCount);
    for (int attempt = 0, traps = 0; attempt < 60 && traps < trapBudget; ++attempt) {
        const FCell cell{kSize / 2 - 2 + random.below(4), kSize / 2 - 2 + random.below(4)};
        if (board[cell.y][cell.x] != '.' || occupied(cell)) continue;
        board[cell.y][cell.x] = '^';
        ++traps;
    }

    WriteTerrain(room, board);
    return room;
}

/// `corridor` (heavy): one wall row across the band with 1-2 gaps of 2-3 cells — the room is crossed only
/// through the corridors (the outer ring stays open around them). Not bound by `obstacleCount`.
FGeneratedRoom GenerateCorridor(const FRoomGenConfig& config, const FRoomRequest& request) {
    FRandom random(Mix(request.seed, request.depth, request.room));
    FBoard board;
    std::vector<FCell> units;
    FGeneratedRoom room = BeginClassicRoom(random, config, request, board, units);
    const auto occupied = [&](FCell cell) { return std::ranges::find(units, cell) != units.end(); };

    const int wallRow = kClutterFirstRow + random.below(kClutterLastRow - kClutterFirstRow + 1);
    const int gapCount = 1 + random.below(2);
    constexpr int kSpan = kSize - 1 - kEdgeMargin - kEdgeMargin + 1;  // columns 1..10
    std::array<bool, kSize> gap{};
    for (int g = 0; g < gapCount; ++g) {
        const int windowStart = kEdgeMargin + g * kSpan / gapCount;
        const int windowEnd = kEdgeMargin + (g + 1) * kSpan / gapCount - 1;
        const int width = std::min(2 + random.below(2), windowEnd - windowStart + 1);
        const int start = windowStart + random.below(windowEnd - windowStart + 1 - width + 1);
        for (int x = start; x < start + width; ++x) gap[static_cast<std::size_t>(x)] = true;
    }
    std::vector<FCell> walls;
    for (int x = kEdgeMargin; x <= kSize - 1 - kEdgeMargin; ++x) {
        if (gap[static_cast<std::size_t>(x)]) continue;
        const FCell cell{x, wallRow};
        if (occupied(cell)) continue;  // a unit standing in the wall row keeps its cell on floor
        board[cell.y][cell.x] = '#';
        walls.push_back(cell);
    }
    std::vector<FCell> removal{walls.rbegin(), walls.rend()};
    DropWallsUntilConnected(board, removal, units);

    const int trapBudget = random.in(config.trapCount);
    PlaceBandTraps(random, board, units, trapBudget);
    WriteTerrain(room, board);
    return room;
}

/// `cover` (heavy): a 3x3 monolithic block at the band centre plus 2-4 single cover cells — firing lines
/// break around the mass. Not bound by `obstacleCount`.
FGeneratedRoom GenerateCover(const FRoomGenConfig& config, const FRoomRequest& request) {
    FRandom random(Mix(request.seed, request.depth, request.room));
    FBoard board;
    std::vector<FCell> units;
    FGeneratedRoom room = BeginClassicRoom(random, config, request, board, units);
    const auto occupied = [&](FCell cell) { return std::ranges::find(units, cell) != units.end(); };

    std::vector<FCell> walls;
    const int blockX = kSize / 2 - 1;
    const int blockY = (kClutterFirstRow + kClutterLastRow) / 2 - 1;
    for (int y = blockY; y < blockY + 3; ++y) {
        for (int x = blockX; x < blockX + 3; ++x) {
            const FCell cell{x, y};
            if (occupied(cell) || board[cell.y][cell.x] != '.') continue;
            board[cell.y][cell.x] = '#';
            walls.push_back(cell);
        }
    }
    const int coverCount = 2 + random.below(3);
    for (int attempt = 0, placed = 0; attempt < 60 && placed < coverCount; ++attempt) {
        const FCell cell{kEdgeMargin + random.below(kSize - 2 * kEdgeMargin),
                         kClutterFirstRow + random.below(kClutterLastRow - kClutterFirstRow + 1)};
        if (board[cell.y][cell.x] != '.' || occupied(cell)) continue;
        board[cell.y][cell.x] = '#';
        if (!AllConnected(board, units)) {
            board[cell.y][cell.x] = '.';
            continue;
        }
        walls.push_back(cell);
        ++placed;
    }
    std::vector<FCell> removal{walls.rbegin(), walls.rend()};
    DropWallsUntilConnected(board, removal, units);

    const int trapBudget = random.in(config.trapCount);
    PlaceBandTraps(random, board, units, trapBudget);
    WriteTerrain(room, board);
    return room;
}

/// `split` (heavy): a dividing wall column with 1-2 door gaps — the room reads as halves joined through the
/// doors (and the open outer ring). Not bound by `obstacleCount`.
FGeneratedRoom GenerateSplit(const FRoomGenConfig& config, const FRoomRequest& request) {
    FRandom random(Mix(request.seed, request.depth, request.room));
    FBoard board;
    std::vector<FCell> units;
    FGeneratedRoom room = BeginClassicRoom(random, config, request, board, units);
    const auto occupied = [&](FCell cell) { return std::ranges::find(units, cell) != units.end(); };

    const int column = kSize / 2 - 1 + random.below(3);
    const int gapCount = 1 + random.below(2);
    constexpr int kGapSpan = kClutterLastRow - kClutterFirstRow - 1;  // the door rows 4..7
    std::array<bool, kSize> gap{};
    for (int g = 0; g < gapCount; ++g) {
        const int windowStart = kClutterFirstRow + 1 + g * kGapSpan / gapCount;
        const int windowEnd = kClutterFirstRow + 1 + (g + 1) * kGapSpan / gapCount - 1;
        const int height = std::min(1 + random.below(2), windowEnd - windowStart + 1);
        const int start = windowStart + random.below(windowEnd - windowStart + 1 - height + 1);
        for (int y = start; y < start + height; ++y) gap[static_cast<std::size_t>(y)] = true;
    }
    std::vector<FCell> walls;
    for (int y = kEnemyFirstRow; y <= kClutterLastRow + 2; ++y) {
        if (gap[static_cast<std::size_t>(y)]) continue;
        const FCell cell{column, y};
        if (occupied(cell)) continue;
        board[cell.y][cell.x] = '#';
        walls.push_back(cell);
    }
    std::vector<FCell> removal{walls.rbegin(), walls.rend()};
    DropWallsUntilConnected(board, removal, units);

    const int trapBudget = random.in(config.trapCount);
    PlaceBandTraps(random, board, units, trapBudget);
    WriteTerrain(room, board);
    return room;
}

// --- boss parts (WP-15) -------------------------------------------------------------------------

/// Appends the boss parts (`config.bossParts`) to a generated boss room. The boss is the first enemy (every
/// family's PlaceEnemies seats it there); the parts take band cells ordered by (Manhattan distance to the
/// boss, y, x) — boss-adjacent first, deterministic. A candidate only seats a part while every unit stays
/// mutually reachable (AllConnected: walls block, units do not — the family invariant). Consumes NO random
/// draws: the classic output stays pinned and non-boss or part-less rooms stay byte-identical. Parts are
/// appended AFTER the escorts (the combat contract); when the band runs out, the rest are skipped
/// (`kMaxBossParts` keeps this far away).
void PlaceBossParts(FGeneratedRoom& room, const FRoomGenConfig& config, const FRoomRequest& request) {
    if (room.enemies.empty()) return;
    const FGeneratedEnemy& boss = room.enemies.front();

    FBoard board{};
    for (int y = 0; y < kSize; ++y) {
        for (int x = 0; x < kSize; ++x) {
            board[y][x] = room.terrain[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)];
        }
    }
    std::vector<FCell> units;
    for (const auto& [x, y] : request.partyCells) units.push_back({x, y});
    for (const auto& enemy : room.enemies) units.push_back({enemy.x, enemy.y});

    // Candidate cells: the spawn band, floor only ('#' walls and '^' traps never host a unit), unoccupied.
    std::vector<FCell> candidates;
    for (int y = kEnemyFirstRow; y <= kEnemyLastRow; ++y) {
        for (int x = kEdgeMargin; x <= kSize - 1 - kEdgeMargin; ++x) {
            const FCell cell{x, y};
            if (board[y][x] != '.') continue;
            if (std::ranges::find(units, cell) != units.end()) continue;
            candidates.push_back(cell);
        }
    }
    std::ranges::sort(candidates, [&](const FCell& a, const FCell& b) {
        const int aDistance = std::abs(a.x - boss.x) + std::abs(a.y - boss.y);
        const int bDistance = std::abs(b.x - boss.x) + std::abs(b.y - boss.y);
        if (aDistance != bDistance) return aDistance < bDistance;
        if (a.y != b.y) return a.y < b.y;
        return a.x < b.x;
    });

    for (const FBossPartSpawn& part : config.bossParts) {
        for (int placed = 0; placed < part.count; ++placed) {
            bool seatFound = false;
            for (std::size_t index = 0; index < candidates.size(); ++index) {
                units.push_back(candidates[index]);
                if (!AllConnected(board, units)) {
                    units.pop_back();
                    continue;
                }
                const FCell seat = candidates[index];
                candidates.erase(candidates.begin() + static_cast<std::ptrdiff_t>(index));
                room.enemies.push_back({part.unit, part.displayName, seat.x, seat.y});
                seatFound = true;
                break;
            }
            if (!seatFound) return;  // the band ran out: stop (kMaxBossParts keeps this far away)
        }
    }
}

// --- measurement helpers ------------------------------------------------------------------------

/// Supercover traversal mirroring gameplay/grid/FGridQuery.cpp HasGridLineOfSight (the game's LoS rule):
/// the original reads the live FGridTerrain from the registry; this is the same walk over a generated
/// board, collecting every examined cell so a blocking wall set can be reported. `from`/`to` are in-grid.
void CollectLineCells(FCell from, FCell to, std::vector<FCell>& out) {
    int x = from.x;
    int y = from.y;
    const int dx = to.x - from.x;
    const int dy = to.y - from.y;
    const int nx = std::abs(dx);
    const int ny = std::abs(dy);
    const int sx = dx > 0 ? 1 : -1;
    const int sy = dy > 0 ? 1 : -1;
    int ix = 0;
    int iy = 0;
    while (ix < nx || iy < ny) {
        const int decision = (1 + 2 * ix) * ny - (1 + 2 * iy) * nx;
        if (decision == 0) {
            out.push_back({x + sx, y});
            out.push_back({x, y + sy});
            x += sx;
            y += sy;
            ++ix;
            ++iy;
        } else if (decision < 0) {
            x += sx;
            ++ix;
        } else {
            y += sy;
            ++iy;
        }
        out.push_back({x, y});
    }
}

bool InGrid(int x, int y) { return x >= 0 && y >= 0 && x < kSize && y < kSize; }

} // namespace

std::optional<EFamilyStrength> FamilyStrengthOf(std::string_view family) {
    if (family == "open" || family == "open_variant") return EFamilyStrength::Light;
    if (family == "corridor" || family == "cover" || family == "split") return EFamilyStrength::Heavy;
    return std::nullopt;  // unknown names are refused by the loader; never a silent "open"
}

std::string_view FamilyStrengthName(EFamilyStrength strength) {
    return strength == EFamilyStrength::Heavy ? "heavy" : "light";
}

FRoomMetrics MeasureRoom(const FGeneratedRoom& room, std::span<const std::pair<int, int>> partyCells) {
    FBoard board{};
    for (int y = 0; y < kSize; ++y) {
        for (int x = 0; x < kSize; ++x) board[y][x] = room.terrain[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)];
    }
    FRoomMetrics metrics;
    for (const auto& row : room.terrain) {
        for (const char cell : row) {
            if (cell == '#') ++metrics.walls;
            else if (cell == '^') ++metrics.traps;
        }
    }
    const int floorCells = kSize * kSize - metrics.walls;

    std::vector<FCell> party;
    for (const auto& [x, y] : partyCells) party.push_back({x, y});

    // Reachable ratio: multi-source BFS from the party; walls block, traps are walkable.
    std::array<bool, kSize * kSize> seen{};
    std::queue<FCell> open;
    int reached = 0;
    for (const FCell cell : party) {
        if (!InGrid(cell.x, cell.y) || board[cell.y][cell.x] == '#' || seen[cell.y * kSize + cell.x]) continue;
        seen[cell.y * kSize + cell.x] = true;
        ++reached;
        open.push(cell);
    }
    while (!open.empty()) {
        const FCell cell = open.front();
        open.pop();
        for (const auto [dx, dy] : {std::pair{1, 0}, std::pair{-1, 0}, std::pair{0, 1}, std::pair{0, -1}}) {
            const int nx = cell.x + dx;
            const int ny = cell.y + dy;
            if (!InGrid(nx, ny) || board[ny][nx] == '#' || seen[ny * kSize + nx]) continue;
            seen[ny * kSize + nx] = true;
            ++reached;
            open.push({nx, ny});
        }
    }
    metrics.reachableRatio = floorCells > 0 ? static_cast<double>(reached) / floorCells : 0.0;

    // min_path: steps from the party to the farthest enemy. Walls block; every other enemy cell blocks too
    // (the formation is walked around), while the target itself is enterable. Party cells are the sources.
    metrics.minPath = 0;
    for (const auto& enemy : room.enemies) {
        const FCell target{enemy.x, enemy.y};
        std::array<bool, kSize * kSize> blocked{};
        for (const auto& other : room.enemies) {
            if (&other == &enemy) continue;
            if (InGrid(other.x, other.y)) blocked[other.y * kSize + other.x] = true;
        }
        std::array<int, kSize * kSize> distance{};
        distance.fill(-1);
        open = {};
        for (const FCell cell : party) {
            if (!InGrid(cell.x, cell.y) || board[cell.y][cell.x] == '#' || distance[cell.y * kSize + cell.x] >= 0) continue;
            distance[cell.y * kSize + cell.x] = 0;
            open.push(cell);
        }
        while (!open.empty()) {
            const FCell cell = open.front();
            open.pop();
            for (const auto [dx, dy] : {std::pair{1, 0}, std::pair{-1, 0}, std::pair{0, 1}, std::pair{0, -1}}) {
                const int nx = cell.x + dx;
                const int ny = cell.y + dy;
                const int index = ny * kSize + nx;
                if (!InGrid(nx, ny) || board[ny][nx] == '#' || distance[index] >= 0) continue;
                if (blocked[index] && !(nx == target.x && ny == target.y)) continue;
                distance[index] = distance[cell.y * kSize + cell.x] + 1;
                open.push({nx, ny});
            }
        }
        const int steps = distance[target.y * kSize + target.x];
        if (steps < 0) {
            metrics.minPath = -1;  // an enemy the party cannot reach: a broken room, visible in the number
            break;
        }
        metrics.minPath = std::max(metrics.minPath, steps);
    }

    // cover_cells: wall cells that block line of sight between at least one party member and one enemy
    // (the supercover rule above — the same cells HasGridLineOfSight would refuse to shoot through).
    std::set<int> covered;
    std::vector<FCell> line;
    for (const FCell from : party) {
        for (const auto& enemy : room.enemies) {
            line.clear();
            CollectLineCells(from, {enemy.x, enemy.y}, line);
            bool blocks = false;
            for (const FCell cell : line) {
                if (InGrid(cell.x, cell.y) && board[cell.y][cell.x] == '#') blocks = true;
            }
            if (!blocks) continue;
            for (const FCell cell : line) {
                if (InGrid(cell.x, cell.y) && board[cell.y][cell.x] == '#') covered.insert(cell.y * kSize + cell.x);
            }
        }
    }
    metrics.coverCells = static_cast<int>(covered.size());
    return metrics;
}

FGeneratedRoom GenerateRoom(const FRoomGenConfig& config, const FRoomRequest& request) {
    // P14-2: the request's family wins when set (WP-14b's elite family flip), else the slot's config family.
    // The loader refuses unknown family names at boot, so the fall-through is the classic layout.
    const std::string& family = request.family.empty() ? config.family : request.family;
    FGeneratedRoom room;
    if (family == "open_variant") room = GenerateOpenVariant(config, request);
    else if (family == "corridor") room = GenerateCorridor(config, request);
    else if (family == "cover") room = GenerateCover(config, request);
    else if (family == "split") room = GenerateSplit(config, request);
    else room = GenerateOpen(config, request);
    // WP-15: boss parts join the document for a boss room (bosses only; no random draws — the classic
    // output is pinned by golden tests).
    if (request.boss && !config.bossParts.empty()) PlaceBossParts(room, config, request);
    return room;
}

} // namespace game::gameplay
