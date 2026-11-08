#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ecs/components/FSkillLoadout.h"
#include "gameplay/data/FContentRegistry.h"
#include "gameplay/progression/FPassiveTree.h"

namespace game {

enum class EPrototypeRoom { Battle, Event, Camp };

struct FPrototypeRoom {
	EPrototypeRoom kind{EPrototypeRoom::Battle};
	std::string title;
	std::string battlefield;
	int experienceReward{0};
};

struct FPreparedMember {
	std::string placementId;
	std::string unitId;
	bool enabled{true};
	std::vector<FSkillSlot> skills;
	int experience{0};
	int level{1};
	int passivePoints{0};
	std::vector<std::string> allocatedNodes;
};

/// Application-owned preparation/checkpoint state. No battle entity handles survive scene unload.
struct FPrototypeRun {
	std::vector<FPrototypeRoom> rooms;
	std::vector<FPreparedMember> party;
	std::vector<std::string> runeInventory;
	FPassiveTree tree;
	int skillSlots{4};
	int experiencePerLevelBase{10};
	int experiencePerLevelStep{5};
	bool active{false};
	std::size_t roomIndex{0};
	unsigned completedDungeons{0};
	unsigned dungeonIndex{0};

	[[nodiscard]] static std::optional<FPrototypeRun> Load(const gameplay::FContentRegistry& content);
	/// WP-32h: prototype-local file seam — reads a passive-tree file with no schema-version gate;
	/// FPassiveTree::Load alone validates the document (v1 and v2). Missing/unreadable files log and yield nullopt.
	[[nodiscard]] static std::optional<FPassiveTree> LoadPassiveTreeFile(const std::filesystem::path& path);
	[[nodiscard]] bool CanStart(const gameplay::FContentRegistry& content) const;
	[[nodiscard]] bool Start(const gameplay::FContentRegistry& content);
	void End();
	void Advance();
	[[nodiscard]] bool ResolveCurrentBattle(bool victory);
	[[nodiscard]] bool CanEditLoadout() const;
	[[nodiscard]] bool EquipRune(
		std::string_view placementId, std::string_view skillId, std::string_view runeId,
		const gameplay::FContentRegistry& content);
	[[nodiscard]] bool UnequipRune(std::string_view placementId, std::string_view skillId, std::string_view runeId);
	[[nodiscard]] bool UnequipSkill(std::string_view placementId, std::string_view skillId);
	[[nodiscard]] bool CanAllocatePassiveNode(std::string_view placementId, std::string_view nodeId) const;
	[[nodiscard]] bool AllocatePassiveNode(std::string_view placementId, std::string_view nodeId);
	[[nodiscard]] int ExperienceForNextLevel(const FPreparedMember& member) const;
	[[nodiscard]] FPassiveStatBonuses PassiveBonuses(std::string_view placementId) const;
	[[nodiscard]] const FPrototypeRoom* CurrentRoom() const;
};

} // namespace game
