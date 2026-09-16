#pragma once

#include <string>
#include <vector>

namespace game::gameplay {

/// The skills this unit may use, as content ids resolved against FContentRegistry (R23: ids, never a
/// string-keyed state bag). `invariant:` every id exists in the skills table — FContentRegistry validated
/// the unit row at boot, so a lookup cannot fail for a live entity.
struct FSkillSet {
    std::vector<std::string> skillIds;
};

} // namespace game::gameplay
