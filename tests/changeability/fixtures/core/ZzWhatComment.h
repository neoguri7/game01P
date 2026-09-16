#pragma once

// V28 [R33] comments that restate the next code line ("what" comments) and carry no why-marker: they rot into
// lies as the code changes (S19 Atwood "comments tell you why", Zucker "always tell me why not"). The
// `// why:` control comment below, and `ecs/components/FZzGood.h`, must NOT be reported.
namespace game::core {
	struct FZzWhatComment {
		float playerPosition{0.0f};
		int comboCount{0};

		void Tick() {
			// player position
			playerPosition += 1.0f;
			// why: mirror of the tick order in MoveSystem, so the combo index cannot outrun the position
			comboCount = static_cast<int>(playerPosition);
			// clamp
			if (comboCount > 8) { comboCount = 8; }
		}
	};
}
