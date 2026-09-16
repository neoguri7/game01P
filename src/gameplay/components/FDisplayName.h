#pragma once

#include <string>

namespace game::gameplay {

/// Text shown for this unit in the battle output. why a copy instead of a content lookup: a later slice may
/// prefix a modifier ("화상 걸린 Proto Melee"), and that name belongs to the instance, not the row.
struct FDisplayName {
    std::string text;
};

} // namespace game::gameplay
