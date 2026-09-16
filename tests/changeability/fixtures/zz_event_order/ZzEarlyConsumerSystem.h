// Calibration fixture for the R3 event-delivery-order detector (D14). DELIBERATELY WRONG: this consumer is
// registered before the ZzPingEvent producer, so beginFrame() clears the queue before anybody reads it.
#pragma once

#include <string>

namespace zz {

struct ZzPingEvent {
    int value{0};
};

struct ZzPongEvent {
    int value{0};
};

struct ZzEarlyConsumerSystem {
    void update() {
        (void)frameEvents<ZzPingEvent>();
        queueFrame<ZzPongEvent>(ZzPongEvent{});
    }
    [[nodiscard]] std::string name() const { return "ZzEarlyConsumer"; }
};

} // namespace zz
