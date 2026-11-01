#include "gameplay/dungeon/FRoomGenerator.h"

#include <algorithm>
#include <queue>

namespace game::gameplay {
namespace {

constexpr int kSize = FGeneratedRoom::kSize;
constexpr int kEdgeMargin = 1;  // the outermost ring stays empty so the room reads as an open map
constexpr int kEnemyFirstRow = 1;
constexpr int kEnemyLastRow = 3;
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

} // namespace

FGeneratedRoom GenerateRoom(const FRoomGenConfig& config, const FRoomRequest& request) {
    FRandom random(Mix(request.seed, request.depth, request.room));
    FGeneratedRoom room;
    room.name = config.names[static_cast<std::size_t>(random.below(static_cast<int>(config.names.size())))];

    FBoard board;
    for (auto& row : board) row.fill('.');

    std::vector<FCell> units;
    for (const auto& [x, y] : request.partyCells) units.push_back({x, y});

    // Enemy formation: random cells in the far (top) rows, one unit per cell.
    std::vector<FCell> slots;
    for (int y = kEnemyFirstRow; y <= kEnemyLastRow; ++y) {
        for (int x = kEdgeMargin; x < kSize - kEdgeMargin; ++x) slots.push_back({x, y});
    }
    for (std::size_t i = slots.size(); i > 1; --i) {
        std::swap(slots[i - 1], slots[static_cast<std::size_t>(random.below(static_cast<int>(i)))]);
    }
    int enemyCount = request.boss ? 3 : random.in(config.enemyCount);
    enemyCount = std::min(enemyCount, static_cast<int>(slots.size()));
    for (int i = 0; i < enemyCount; ++i) {
        FGeneratedEnemy enemy;
        if (request.boss && i == 0) {
            enemy.unit = config.bossUnit;
            enemy.displayName = config.bossName;
        } else {
            const auto& choice = PickEnemy(random, config.enemies);
            enemy.unit = choice.unit;
            enemy.displayName = choice.displayName;
        }
        enemy.x = slots[static_cast<std::size_t>(i)].x;
        enemy.y = slots[static_cast<std::size_t>(i)].y;
        units.push_back({enemy.x, enemy.y});
        room.enemies.push_back(std::move(enemy));
    }

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

    for (int y = 0; y < kSize; ++y) room.terrain[static_cast<std::size_t>(y)] = std::string(board[y].begin(), board[y].end());
    return room;
}

} // namespace game::gameplay
