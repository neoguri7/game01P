// Calibration fixture (D14) negative controls: a system reading back its own frame event (read-after-write in one
// system, legal) and a consumer of a type nobody queues (dead vocabulary is R9's business, not an ordering bug).
#pragma once

#include <string>

namespace zz {

struct ZzSelfEvent {
    int value{0};
};

struct ZzSelfReadSystem {
    void update() {
        queueFrame<ZzSelfEvent>(ZzSelfEvent{});
        (void)frameEvents<ZzSelfEvent>();
    }
    [[nodiscard]] std::string name() const { return "ZzSelfRead"; }
};

struct ZzOrphanConsumerSystem {
    void update() { (void)frameEvents<ZzNobodyEvent>(); }
    [[nodiscard]] std::string name() const { return "ZzOrphanConsumer"; }
};

} // namespace zz
