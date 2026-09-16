// Calibration fixture (D14): the producer of the event the early consumer reads. Registered after it.
#pragma once

#include <string>

namespace zz {

struct ZzLateProducerSystem {
    void update() { queueFrame<ZzPingEvent>(ZzPingEvent{}); }
    [[nodiscard]] std::string name() const { return "ZzLateProducer"; }
};

} // namespace zz
