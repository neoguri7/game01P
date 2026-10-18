#include "gameplay/progression/FPassiveTree.h"

#include <algorithm>
#include <cmath>
#include <set>

#include <nlohmann/json.hpp>

#include "core/Logger.h"

namespace game {

std::optional<FPassiveTree> FPassiveTree::Load(const nlohmann::json& document) {
    FPassiveTree tree;
    try {
        if (!document.at("schema_version").is_number_integer() || document.at("schema_version") != 1) {
            LOG_ERROR("passive tree needs schema_version 1.");
            return std::nullopt;
        }
        std::set<std::string> nodeIds;
        for (const auto& row : document.at("nodes")) {
            FPassiveNode node;
            node.id = row.at("id").get<std::string>();
            node.displayName = row.at("display_name").get<std::string>();
            node.description = row.at("description").get<std::string>();
            if (!row.at("parent").is_null()) node.parentId = row.at("parent").get<std::string>();
            node.x = row.at("x").get<float>();
            node.y = row.at("y").get<float>();
            const auto& bonuses = row.at("bonuses");
            node.bonuses.maxHealth = bonuses.at("max_health").get<int>();
            node.bonuses.attack = bonuses.at("attack").get<int>();
            node.bonuses.magicAttack = bonuses.at("magic_attack").get<int>();
            node.bonuses.defense = bonuses.at("defense").get<int>();
            node.bonuses.magicDefense = bonuses.at("magic_defense").get<int>();
            node.bonuses.speed = bonuses.at("speed").get<int>();
            const bool validBonuses = node.bonuses.maxHealth >= 0 && node.bonuses.attack >= 0
                && node.bonuses.magicAttack >= 0 && node.bonuses.defense >= 0
                && node.bonuses.magicDefense >= 0 && node.bonuses.speed >= 0;
            if (node.id.empty() || node.displayName.empty() || node.description.empty()
                || !nodeIds.insert(node.id).second || !std::isfinite(node.x) || !std::isfinite(node.y)
                || node.x < 0.0f || node.x > 100.0f || node.y < 0.0f || node.y > 100.0f || !validBonuses) {
                LOG_ERROR("passive tree node '{}' is invalid.", node.id);
                return std::nullopt;
            }
            if (node.parentId.empty()) {
                if (!tree.rootNodeId.empty() || node.bonuses.maxHealth != 0 || node.bonuses.attack != 0
                    || node.bonuses.magicAttack != 0 || node.bonuses.defense != 0
                    || node.bonuses.magicDefense != 0 || node.bonuses.speed != 0) {
                    LOG_ERROR("passive tree needs exactly one bonus-free root node.");
                    return std::nullopt;
                }
                tree.rootNodeId = node.id;
            }
            tree.nodes.push_back(std::move(node));
        }
        if (tree.nodes.size() < 2 || tree.rootNodeId.empty()) {
            LOG_ERROR("passive tree needs a root and at least one child node.");
            return std::nullopt;
        }
        for (const FPassiveNode& node : tree.nodes) {
            std::set<std::string> visited{node.id};
            std::string parentId = node.parentId;
            while (!parentId.empty()) {
                if (!visited.insert(parentId).second) {
                    LOG_ERROR("passive tree node '{}' forms a parent cycle.", node.id);
                    return std::nullopt;
                }
                const auto parent = std::ranges::find(tree.nodes, parentId, &FPassiveNode::id);
                if (parent == tree.nodes.end()) {
                    LOG_ERROR("passive tree node '{}' references unknown parent '{}'.", node.id, parentId);
                    return std::nullopt;
                }
                parentId = parent->parentId;
            }
        }
    } catch (const nlohmann::json::exception& error) {
        LOG_ERROR("passive tree document is malformed: {}", error.what());
        return std::nullopt;
    }
    return tree;
}

const FPassiveNode* FPassiveTree::Find(std::string_view nodeId) const {
    const auto found = std::ranges::find(nodes, nodeId, &FPassiveNode::id);
    return found == nodes.end() ? nullptr : &*found;
}

bool FPassiveTree::CanAllocate(const std::vector<std::string>& allocated, std::string_view nodeId) const {
    const FPassiveNode* node = Find(nodeId);
    if (node == nullptr || node->id == rootNodeId) return false;
    if (std::ranges::find(allocated, nodeId) != allocated.end()) return false;
    return node->parentId == rootNodeId
        || std::ranges::find(allocated, node->parentId) != allocated.end();
}

FPassiveStatBonuses FPassiveTree::Bonuses(const std::vector<std::string>& allocated) const {
    FPassiveStatBonuses total;
    for (const std::string& nodeId : allocated) {
        const FPassiveNode* node = Find(nodeId);
        if (node == nullptr) continue;
        total.maxHealth += node->bonuses.maxHealth;
        total.attack += node->bonuses.attack;
        total.magicAttack += node->bonuses.magicAttack;
        total.defense += node->bonuses.defense;
        total.magicDefense += node->bonuses.magicDefense;
        total.speed += node->bonuses.speed;
    }
    return total;
}

} // namespace game
