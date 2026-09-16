// Calibration fixture (D14) negative control: the legal shape — producer first, consumer after it.
#pragma once

#include <string>

namespace zz {

struct ZzBeatEvent {
    int value{0};
};

struct ZzBeatProducerSystem {
    void update() { queueFrame<ZzBeatEvent>(ZzBeatEvent{}); }
    [[nodiscard]] std::string name() const { return "ZzBeatProducer"; }
};

struct ZzBeatConsumerSystem {
    void update() { (void)frameEvents<ZzBeatEvent>(); }
    [[nodiscard]] std::string name() const { return "ZzBeatConsumer"; }
};

} // namespace zz
