#include "gameplay/progression/FPassiveTree.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/Logger.h"

namespace game {
namespace {

/// v2 `max_ranks`의 상한 — 이보다 큰 랭크를 선언하면 로드 실패다.
constexpr int kMaxPassiveNodeRanks = 9;

/// 단일 보너스/캡 값의 상한 — 밸런스 대역(+2/+6)의 수십 배이고, int64→int 절단으로 가짜 소값이
/// 통과하는 것을 막는 상한이다.
constexpr int kMaxPassiveBonus = 999;

/// 보너스 키 표 — `bonuses`의 json 키를 FPassiveStatBonuses 멤버에 잇는다.
struct FBonusField {
    const char* key;
    int FPassiveStatBonuses::*member;
};

constexpr FBonusField kBonusFields[] = {
    {"max_health", &FPassiveStatBonuses::maxHealth},
    {"attack", &FPassiveStatBonuses::attack},
    {"magic_attack", &FPassiveStatBonuses::magicAttack},
    {"defense", &FPassiveStatBonuses::defense},
    {"magic_defense", &FPassiveStatBonuses::magicDefense},
    {"speed", &FPassiveStatBonuses::speed},
};

/// 캡 키 표 — kBonusFields와 같은 스탯을 같은 순서로 가리킨다 (클램프가 두 표를 이어 쓴다).
struct FCapsField {
    const char* key;
    std::optional<int> FPassiveStatCaps::*cap;
    int FPassiveStatBonuses::*bonus;
};

constexpr FCapsField kCapsFields[] = {
    {"max_total_health", &FPassiveStatCaps::maxHealth, &FPassiveStatBonuses::maxHealth},
    {"max_total_attack", &FPassiveStatCaps::attack, &FPassiveStatBonuses::attack},
    {"max_total_magic_attack", &FPassiveStatCaps::magicAttack, &FPassiveStatBonuses::magicAttack},
    {"max_total_defense", &FPassiveStatCaps::defense, &FPassiveStatBonuses::defense},
    {"max_total_magic_defense", &FPassiveStatCaps::magicDefense, &FPassiveStatBonuses::magicDefense},
    {"max_total_speed", &FPassiveStatCaps::speed, &FPassiveStatBonuses::speed},
};

/// 방향 엣지 하나 — 노드 인덱스 쌍 (from → to).
struct FDirectedEdge {
    std::size_t from{0};
    std::size_t to{0};
};

/// 문자열 키 하나를 읽는다 — 없거나 문자열이 아니면 false.
bool ReadString(const nlohmann::json& row, const char* key, std::string& out) {
    const auto value = row.find(key);
    if (value == row.end() || !value->is_string()) return false;
    out = value->get<std::string>();
    return true;
}

/// json 정수를 int64로 읽고 [lowest, highest] 범위를 확인한다 — get<int>의 무음 절단
/// (예: max_ranks 4294967297 → 1)을 막는다.
std::optional<int> ReadBoundedInt(const nlohmann::json& value, int lowest, int highest) {
    if (!value.is_number_integer()) return std::nullopt;
    const std::int64_t wide = value.get<std::int64_t>();
    if (wide < lowest || wide > highest) return std::nullopt;
    return static_cast<int>(wide);
}

/// `bonuses`: 여섯 키 전부, 정수 0..kMaxPassiveBonus (기존 의미 불변).
bool ParseBonuses(const nlohmann::json& row, FPassiveStatBonuses& bonuses) {
    const auto table = row.find("bonuses");
    if (table == row.end() || !table->is_object()) return false;
    for (const FBonusField& field : kBonusFields) {
        const auto value = table->find(field.key);
        if (value == table->end()) return false;
        const auto amount = ReadBoundedInt(*value, 0, kMaxPassiveBonus);
        if (!amount) return false;
        bonuses.*(field.member) = *amount;
    }
    return true;
}

/// 노드 공통: 비어 있지 않은 id/display_name/description, 유한한 [0,100] 좌표, 여섯 보너스.
bool ParseNodeCore(const nlohmann::json& row, FPassiveNode& node) {
    if (!row.is_object() || !ReadString(row, "id", node.id) || !ReadString(row, "display_name", node.displayName)
        || !ReadString(row, "description", node.description)) {
        LOG_ERROR("passive tree node needs string id, display_name and description.");
        return false;
    }
    if (node.id.empty() || node.displayName.empty() || node.description.empty()) {
        LOG_ERROR("passive tree node '{}' has an empty id, display_name or description.", node.id);
        return false;
    }
    const auto x = row.find("x");
    const auto y = row.find("y");
    if (x == row.end() || !x->is_number() || y == row.end() || !y->is_number()) {
        LOG_ERROR("passive tree node '{}' needs numeric x and y.", node.id);
        return false;
    }
    node.x = x->get<float>();
    node.y = y->get<float>();
    if (!std::isfinite(node.x) || !std::isfinite(node.y) || node.x < 0.0f || node.x > 100.0f || node.y < 0.0f
        || node.y > 100.0f) {
        LOG_ERROR("passive tree node '{}' coordinates must lie in [0,100].", node.id);
        return false;
    }
    if (!ParseBonuses(row, node.bonuses)) {
        LOG_ERROR("passive tree node '{}' bonuses need six nonnegative integers.", node.id);
        return false;
    }
    return true;
}

/// v1 추가 필드: `parent` 필수(null/id). schema 2 키(next/max_ranks/exclusive_group)는 조용히 무시하지 않는다.
bool ParseV1Extras(const nlohmann::json& row, const FPassiveNode& node, std::string& parent) {
    if (row.contains("next") || row.contains("max_ranks") || row.contains("exclusive_group")) {
        LOG_ERROR("passive tree node '{}' uses a schema 2 key in a schema 1 document.", node.id);
        return false;
    }
    const auto value = row.find("parent");
    if (value == row.end() || !(value->is_null() || value->is_string())) {
        LOG_ERROR("passive tree node '{}' needs a parent (null or node id) in schema 1.", node.id);
        return false;
    }
    if (value->is_string()) {
        parent = value->get<std::string>();
        if (parent.empty()) {
            LOG_ERROR("passive tree node '{}' declares an empty parent id.", node.id);
            return false;
        }
    }
    return true;
}

/// v2 추가 필드: `next`(문자열 배열), `parent`(문자열/null), `max_ranks`(정수), `exclusive_group`(문자열).
bool ParseV2Extras(const nlohmann::json& row, FPassiveNode& node, std::string& parent) {
    if (const auto value = row.find("next"); value != row.end()) {
        if (!value->is_array()) {
            LOG_ERROR("passive tree node '{}' next must be an array of node ids.", node.id);
            return false;
        }
        for (const auto& entry : *value) {
            if (!entry.is_string() || entry.get<std::string>().empty()) {
                LOG_ERROR("passive tree node '{}' next carries a non-string or empty node id.", node.id);
                return false;
            }
            node.next.push_back(entry.get<std::string>());
        }
    }
    if (const auto value = row.find("parent"); value != row.end() && !value->is_null()) {
        if (!value->is_string() || value->get<std::string>().empty()) {
            LOG_ERROR("passive tree node '{}' parent must be null or a non-empty node id.", node.id);
            return false;
        }
        parent = value->get<std::string>();
    }
    if (const auto value = row.find("max_ranks"); value != row.end()) {
        if (!value->is_number_integer()) {
            LOG_ERROR("passive tree node '{}' max_ranks must be an integer.", node.id);
            return false;
        }
        const auto ranks = ReadBoundedInt(*value, 1, kMaxPassiveNodeRanks);
        if (!ranks) {
            LOG_ERROR(
                "passive tree node '{}' max_ranks {} must lie in 1..{}.",
                node.id,
                value->dump(),
                kMaxPassiveNodeRanks);
            return false;
        }
        node.maxRanks = *ranks;
    }
    if (const auto value = row.find("exclusive_group"); value != row.end()) {
        if (!value->is_string() || value->get<std::string>().empty()) {
            LOG_ERROR("passive tree node '{}' exclusive_group must be a non-empty string.", node.id);
            return false;
        }
        node.exclusiveGroup = value->get<std::string>();
    }
    return true;
}

/// `schema_version`: 정수 1 또는 2 — 그 외에는 nullopt.
std::optional<int> ParseSchemaVersion(const nlohmann::json& document) {
    const auto version = document.find("schema_version");
    if (version == document.end()) return std::nullopt;
    return ReadBoundedInt(*version, 1, 2);
}

/// 파싱된 노드 목록 + 노드별 `parent` 선언 (병렬 벡터, "" = 선언 없음).
struct FParsedNodes {
    FPassiveTree tree;
    std::vector<std::string> parents;
};

/// `nodes` 배열 전체를 읽는다 — 스키마별 추가 필드, id 유일성, 최소 노드 수.
std::optional<FParsedNodes> ParseNodes(const nlohmann::json& document, int schemaVersion) {
    const auto rows = document.find("nodes");
    if (rows == document.end() || !rows->is_array()) {
        LOG_ERROR("passive tree needs a nodes array.");
        return std::nullopt;
    }
    FParsedNodes parsed;
    std::set<std::string> ids;
    for (const auto& row : *rows) {
        FPassiveNode node;
        std::string parent;
        const bool fieldsOk = ParseNodeCore(row, node)
            && (schemaVersion == 1 ? ParseV1Extras(row, node, parent) : ParseV2Extras(row, node, parent));
        if (!fieldsOk) return std::nullopt;
        if (!ids.insert(node.id).second) {
            LOG_ERROR("passive tree node id '{}' repeats.", node.id);
            return std::nullopt;
        }
        parsed.parents.push_back(std::move(parent));
        parsed.tree.nodes.push_back(std::move(node));
    }
    if (parsed.tree.nodes.size() < 2) {
        LOG_ERROR("passive tree needs a root and at least one child node.");
        return std::nullopt;
    }
    return parsed;
}

/// 저작된 방향 엣지를 문서 순서대로 모은다: 각 노드의 `next`(배열 순서)가 먼저, 그다음 각 노드의
/// `parent` 선언이 온다. `parent: P`는 P.next에도 정규화된다(v1 호환). 같은 엣지의 이중 선언은 실패다.
std::optional<std::vector<FDirectedEdge>> CollectEdges(
    FPassiveTree& tree, const std::vector<std::string>& parents, const std::map<std::string, std::size_t>& index) {
    std::vector<FDirectedEdge> edges;
    std::set<std::pair<std::size_t, std::size_t>> seen;
    const auto addEdge = [&](std::size_t from, std::size_t to) {
        if (!seen.insert({from, to}).second) {
            LOG_ERROR("passive tree declares edge '{}' -> '{}' twice.", tree.nodes[from].id, tree.nodes[to].id);
            return false;
        }
        edges.push_back(FDirectedEdge{from, to});
        return true;
    };
    for (std::size_t i = 0; i < tree.nodes.size(); ++i) {
        for (const std::string& targetId : tree.nodes[i].next) {
            const auto target = index.find(targetId);
            if (target == index.end()) {
                LOG_ERROR("passive tree node '{}' references unknown neighbor '{}'.", tree.nodes[i].id, targetId);
                return std::nullopt;
            }
            if (target->second == i) {
                LOG_ERROR("passive tree node '{}' points at itself.", tree.nodes[i].id);
                return std::nullopt;
            }
            if (!addEdge(i, target->second)) return std::nullopt;
        }
    }
    for (std::size_t i = 0; i < tree.nodes.size(); ++i) {
        if (parents[i].empty()) continue;
        const auto source = index.find(parents[i]);
        if (source == index.end()) {
            LOG_ERROR("passive tree node '{}' references unknown parent '{}'.", tree.nodes[i].id, parents[i]);
            return std::nullopt;
        }
        if (source->second == i) {
            LOG_ERROR("passive tree node '{}' points at itself.", tree.nodes[i].id);
            return std::nullopt;
        }
        if (!addEdge(source->second, i)) return std::nullopt;
        tree.nodes[source->second].next.push_back(tree.nodes[i].id);
    }
    return edges;
}

/// 진입 엣지가 하나도 없는 노드가 정확히 하나(그것이 루트)이고, 그 노드의 보너스가 전부 0인가.
std::optional<std::size_t> FindRoot(const FPassiveTree& tree, const std::vector<FDirectedEdge>& edges) {
    std::vector<int> incoming(tree.nodes.size(), 0);
    for (const FDirectedEdge& edge : edges) ++incoming[edge.to];
    std::optional<std::size_t> root;
    for (std::size_t i = 0; i < tree.nodes.size(); ++i) {
        if (incoming[i] != 0) continue;
        if (root.has_value()) {
            LOG_ERROR("passive tree node '{}' has no incoming edge — a second root.", tree.nodes[i].id);
            return std::nullopt;
        }
        root = i;
    }
    if (!root.has_value()) {
        LOG_ERROR("passive tree has no root (every node has an incoming edge).");
        return std::nullopt;
    }
    const bool bonusFree = std::ranges::all_of(kBonusFields, [&](const FBonusField& field) {
        return tree.nodes[*root].bonuses.*(field.member) == 0;
    });
    if (!bonusFree) {
        LOG_ERROR("passive tree root '{}' must be bonus-free.", tree.nodes[*root].id);
        return std::nullopt;
    }
    return root;
}

enum class EVisitColor : std::uint8_t { White, Gray, Black };

/// 방향 사이클 검사 (DFS 3색) — 수렴(다이아몬드)은 정상이고, 회색으로 되돌아오는 엣지가 실패다.
bool VisitForCycle(
    const std::vector<FPassiveNode>& nodes, const std::map<std::string, std::size_t>& index, std::size_t node,
    std::vector<EVisitColor>& colors) {
    colors[node] = EVisitColor::Gray;
    for (const std::string& targetId : nodes[node].next) {
        const std::size_t target = index.at(targetId);
        if (colors[target] == EVisitColor::Gray) {
            LOG_ERROR("passive tree edges form a directed cycle through '{}'.", targetId);
            return false;
        }
        if (colors[target] == EVisitColor::White && !VisitForCycle(nodes, index, target, colors)) return false;
    }
    colors[node] = EVisitColor::Black;
    return true;
}

bool HasDirectedCycle(const std::vector<FPassiveNode>& nodes, const std::map<std::string, std::size_t>& index) {
    std::vector<EVisitColor> colors(nodes.size(), EVisitColor::White);
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        if (colors[i] == EVisitColor::White && !VisitForCycle(nodes, index, i, colors)) return true;
    }
    return false;
}

void PushUnique(std::vector<std::string>& list, const std::string& value) {
    if (std::ranges::find(list, value) == list.end()) list.push_back(value);
}

/// 무방향 할당 인접: 나가는 엣지(next 순서) → 들어오는 엣지(문서 순서), 중복 없음.
void FillNeighbors(FPassiveTree& tree, const std::vector<FDirectedEdge>& edges) {
    for (FPassiveNode& node : tree.nodes) {
        for (const std::string& targetId : node.next) PushUnique(node.neighbors, targetId);
    }
    for (const FDirectedEdge& edge : edges) PushUnique(tree.nodes[edge.to].neighbors, tree.nodes[edge.from].id);
}

/// 루트에서 무방향 인접(neighbors)으로 모든 노드에 도달 가능한가 — 고립 컴포넌트는 실패다.
bool UndirectedReachesAll(const FPassiveTree& tree, const std::map<std::string, std::size_t>& index, std::size_t root) {
    std::vector<bool> visited(tree.nodes.size(), false);
    std::vector<std::size_t> frontier{root};
    visited[root] = true;
    for (std::size_t head = 0; head < frontier.size(); ++head) {
        for (const std::string& neighborId : tree.nodes[frontier[head]].neighbors) {
            const std::size_t neighbor = index.at(neighborId);
            if (visited[neighbor]) continue;
            visited[neighbor] = true;
            frontier.push_back(neighbor);
        }
    }
    for (std::size_t i = 0; i < visited.size(); ++i) {
        if (visited[i]) continue;
        LOG_ERROR("passive tree node '{}' is not connected to the root.", tree.nodes[i].id);
        return false;
    }
    return true;
}

/// max_ranks: 범위 1..kMaxPassiveNodeRanks, 그리고 루트는 반복할 수 없다(1).
bool ValidateRanks(const FPassiveTree& tree, std::size_t root) {
    for (std::size_t i = 0; i < tree.nodes.size(); ++i) {
        const FPassiveNode& node = tree.nodes[i];
        if (node.maxRanks < 1 || node.maxRanks > kMaxPassiveNodeRanks) {
            LOG_ERROR(
                "passive tree node '{}' max_ranks {} must lie in 1..{}.",
                node.id,
                node.maxRanks,
                kMaxPassiveNodeRanks);
            return false;
        }
        if (i == root && node.maxRanks != 1) {
            LOG_ERROR("passive tree root '{}' cannot repeat.", node.id);
            return false;
        }
    }
    return true;
}

/// exclusive_group: 루트는 가질 수 없고, 모든 그룹은 구성원이 둘 이상이다.
bool ValidateExclusiveGroups(const FPassiveTree& tree, std::size_t root) {
    if (!tree.nodes[root].exclusiveGroup.empty()) {
        LOG_ERROR("passive tree root '{}' cannot join an exclusive group.", tree.nodes[root].id);
        return false;
    }
    for (const FPassiveNode& node : tree.nodes) {
        if (node.exclusiveGroup.empty()) continue;
        const auto members = std::ranges::count_if(tree.nodes, [&](const FPassiveNode& other) {
            return other.exclusiveGroup == node.exclusiveGroup;
        });
        if (members < 2) {
            LOG_ERROR("passive tree exclusive group '{}' needs at least two members.", node.exclusiveGroup);
            return false;
        }
    }
    return true;
}

/// v2 `caps`(선택): 키는 여섯 캡 키로 한정, 값은 정수 0..kMaxPassiveBonus. 미지 키는 조용히 무시하지 않는다.
bool ParseCaps(const nlohmann::json& document, FPassiveStatCaps& caps) {
    const auto table = document.find("caps");
    if (table == document.end()) return true;
    if (!table->is_object()) {
        LOG_ERROR("passive tree caps must be an object of nonnegative integers.");
        return false;
    }
    for (auto entry = table->begin(); entry != table->end(); ++entry) {
        const FCapsField* field = nullptr;
        for (const FCapsField& candidate : kCapsFields) {
            if (entry.key() == candidate.key) {
                field = &candidate;
                break;
            }
        }
        if (field == nullptr) {
            LOG_ERROR("passive tree caps key '{}' is unknown.", entry.key());
            return false;
        }
        const auto amount = ReadBoundedInt(entry.value(), 0, kMaxPassiveBonus);
        if (!amount) {
            LOG_ERROR("passive tree cap '{}' must be a nonnegative integer.", entry.key());
            return false;
        }
        caps.*(field->cap) = *amount;
    }
    return true;
}

/// parentId = 문서 순서상 첫 진입 엣지의 출발 노드 (표시·구 호출자용 — 할당은 neighbors를 쓴다).
void FillParents(FPassiveTree& tree, const std::vector<FDirectedEdge>& edges) {
    for (const FDirectedEdge& edge : edges) {
        FPassiveNode& node = tree.nodes[edge.to];
        if (node.parentId.empty()) node.parentId = tree.nodes[edge.from].id;
    }
}

} // namespace

std::optional<FPassiveTree> FPassiveTree::Load(const nlohmann::json& document) {
    try {
        const auto schemaVersion = ParseSchemaVersion(document);
        if (!schemaVersion) {
            LOG_ERROR("passive tree needs schema_version 1 or 2.");
            return std::nullopt;
        }
        if (*schemaVersion == 1 && document.contains("caps")) {
            LOG_ERROR("passive tree schema 1 cannot carry caps.");
            return std::nullopt;
        }
        auto parsed = ParseNodes(document, *schemaVersion);
        if (!parsed) return std::nullopt;
        std::map<std::string, std::size_t> index;
        for (std::size_t i = 0; i < parsed->tree.nodes.size(); ++i) index.emplace(parsed->tree.nodes[i].id, i);
        auto edges = CollectEdges(parsed->tree, parsed->parents, index);
        if (!edges) return std::nullopt;
        const auto root = FindRoot(parsed->tree, *edges);
        if (!root) return std::nullopt;
        if (HasDirectedCycle(parsed->tree.nodes, index)) return std::nullopt;
        FillNeighbors(parsed->tree, *edges);
        if (!UndirectedReachesAll(parsed->tree, index, *root)) return std::nullopt;
        if (!ValidateRanks(parsed->tree, *root)) return std::nullopt;
        if (!ValidateExclusiveGroups(parsed->tree, *root)) return std::nullopt;
        if (*schemaVersion == 2 && !ParseCaps(document, parsed->tree.caps)) return std::nullopt;
        FillParents(parsed->tree, *edges);
        parsed->tree.rootNodeId = parsed->tree.nodes[*root].id;
        return std::move(parsed->tree);
    } catch (const nlohmann::json::exception& error) {
        LOG_ERROR("passive tree document is malformed: {}", error.what());
        return std::nullopt;
    }
}

const FPassiveNode* FPassiveTree::Find(std::string_view nodeId) const {
    const auto found = std::ranges::find(nodes, nodeId, &FPassiveNode::id);
    return found == nodes.end() ? nullptr : &*found;
}

int PassiveNodeRank(const std::vector<std::string>& allocated, std::string_view nodeId) {
    return static_cast<int>(std::ranges::count(allocated, nodeId));
}

const FPassiveNode* PassiveExclusiveConflict(
    const std::vector<FPassiveNode>& nodes, const std::vector<std::string>& allocated, std::string_view nodeId) {
    const auto asked = std::ranges::find(nodes, nodeId, &FPassiveNode::id);
    if (asked == nodes.end() || asked->exclusiveGroup.empty()) return nullptr;
    for (const FPassiveNode& candidate : nodes) {
        if (candidate.id == asked->id) continue;
        if (candidate.exclusiveGroup == asked->exclusiveGroup
            && std::ranges::find(allocated, candidate.id) != allocated.end()) {
            return &candidate;
        }
    }
    return nullptr;
}

bool FPassiveTree::CanAllocate(const std::vector<std::string>& allocated, std::string_view nodeId) const {
    const FPassiveNode* node = Find(nodeId);
    if (node == nullptr || node->id == rootNodeId) return false;
    const int rank = PassiveNodeRank(allocated, nodeId);
    if (rank >= node->maxRanks) return false;
    if (PassiveExclusiveConflict(nodes, allocated, nodeId) != nullptr) return false;
    if (rank >= 1) return true;  // 반복 노드 재할당 = 자기 인접.
    return std::ranges::any_of(node->neighbors, [&](const std::string& neighborId) {
        return neighborId == rootNodeId || std::ranges::find(allocated, neighborId) != allocated.end();
    });
}

FPassiveStatBonuses FPassiveTree::Bonuses(const std::vector<std::string>& allocated) const {
    FPassiveStatBonuses total;
    for (const std::string& nodeId : allocated) {
        const FPassiveNode* node = Find(nodeId);
        if (node == nullptr) continue;
        // 여섯 스탯의 합산 경로를 kBonusFields 표 하나로 모은다 (반복 = 등장 수만큼 중복 가산).
        for (const FBonusField& field : kBonusFields) {
            total.*(field.member) += node->bonuses.*(field.member);
        }
    }
    // 캡 클램프 — 설정된 스탯만 트리 몫을 min(합, cap)으로 자른다 (미설정 = 무제한).
    for (const FCapsField& field : kCapsFields) {
        const std::optional<int> cap = caps.*(field.cap);
        if (!cap.has_value()) continue;
        total.*(field.bonus) = std::min(total.*(field.bonus), *cap);
    }
    return total;
}

} // namespace game
