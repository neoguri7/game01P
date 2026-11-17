#include "debug/TurnDemo.h"
#include "ecs/components/FSpeed.h"
#include "ecs/components/FTurnParticipant.h"
#include "ecs/systems/TurnSystem.h"
#include <imgui.h>

namespace game {
namespace {

const char* EntryStateName(ETurnEntryState state) {
	switch (state) {
	case ETurnEntryState::Pending: return "Pending";
	case ETurnEntryState::Active: return "Active";
	case ETurnEntryState::Completed: return "Completed";
	case ETurnEntryState::Skipped: return "Skipped";
	}
	return "Unknown";
}

} // namespace

void RenderTurnDemo(entt::registry& registry) {
	auto* demo = registry.ctx().find<FTurnDemo>();
	auto* runtime = registry.ctx().find<FTurnRuntime>();
	if (!demo || !runtime) return;
	if (!ImGui::Begin("Turn Demo")) {
		ImGui::End();
		return;
	}
	const auto snapshot = runtime->controller.snapshot();
	ImGui::InputScalar("Seed", ImGuiDataType_U32, &demo->seed);
	ImGui::Text("State: %s | Session: %llu | Round: %llu",
		snapshot.state == ETurnState::Running ? "Running" : "Idle",
		static_cast<unsigned long long>(snapshot.sessionId),
		static_cast<unsigned long long>(snapshot.roundNumber));
	if (snapshot.currentTurn) {
		ImGui::Text("Current: %s", TurnDemoLabel(registry, snapshot.currentTurn->actor).c_str());
	} else {
		ImGui::TextUnformatted("Current: none");
	}
	if (ImGui::Button("Initialize / reset demo")) demo->requests.emplace_back(FResetTurnDemo{});
	if (ImGui::Button("Start")) runtime->requests.emplace_back(FStartTurnSession{demo->seed});
	ImGui::SameLine();
	ImGui::BeginDisabled(!snapshot.currentTurn);
	if (ImGui::Button("End current turn")) runtime->requests.emplace_back(FEndTurn{*snapshot.currentTurn});
	ImGui::EndDisabled();
	ImGui::SameLine();
	if (ImGui::Button("Stop")) runtime->requests.emplace_back(FStopTurnSession{});
	if (runtime->lastError) ImGui::Text("Turn request error: %s", TurnErrorName(*runtime->lastError));
	if (demo->lastError) ImGui::Text("Demo request error: %s", TurnErrorName(*demo->lastError));

	ImGui::SeparatorText("Fixed current round");
	for (std::size_t index = 0; index < snapshot.entries.size(); ++index) {
		const auto& entry = snapshot.entries[index];
		const auto* live = registry.valid(entry.entity) ? registry.try_get<FSpeed>(entry.entity) : nullptr;
		ImGui::Text("%zu. %s | %s | round speed: %d", index + 1,
			TurnDemoLabel(registry, entry.entity).c_str(), EntryStateName(entry.state), static_cast<int>(entry.speed));
		if (live) {
			ImGui::SameLine();
			ImGui::Text("| live: %d%s", static_cast<int>(live->value),
				live->value != entry.speed ? " (applies next round)" : "");
		}
	}
	ImGui::TextDisabled("Ties are drawn again at the next round; its order is not yet known.");
	ImGui::SeparatorText("Owned demo units");
	for (const auto& unit : demo->units) {
		ImGui::PushID(static_cast<int>(entt::to_integral(unit.entity)));
		ImGui::TextUnformatted(TurnDemoLabel(registry, unit.entity).c_str());
		if (registry.valid(unit.entity)) {
			if (const auto* live = registry.try_get<FSpeed>(unit.entity)) {
				int speed = static_cast<int>(live->value);
				if (ImGui::InputInt("Live speed", &speed)) {
					demo->requests.emplace_back(FSetTurnDemoSpeed{unit.entity, static_cast<std::int32_t>(speed)});
				}
			}
			bool participating = registry.all_of<FTurnParticipant>(unit.entity);
			if (ImGui::Checkbox("Participating", &participating)) {
				demo->requests.emplace_back(FSetTurnDemoParticipation{unit.entity, participating});
			}
			if (ImGui::Button("Delete")) demo->requests.emplace_back(FDeleteTurnDemoUnit{unit.entity});
		}
		ImGui::PopID();
	}
	int addedSpeed = static_cast<int>(demo->addedSpeed);
	if (ImGui::InputInt("New unit speed", &addedSpeed)) demo->addedSpeed = static_cast<std::int32_t>(addedSpeed);
	if (ImGui::Button("Add unit")) demo->requests.emplace_back(FAddTurnDemoUnit{demo->addedSpeed});
	ImGui::End();
}

} // namespace game
