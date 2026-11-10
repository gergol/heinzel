#pragma once

// A DELIBERATE OFFENDER for the frontend_decides_nothing_semantic gate, beside `defers/`: the plain lower-case
// spelling of deferred machinery, which the gate's "pending after no lower-case letter" branch is there to catch.
// Nothing includes this file.
#include <vector>

namespace heinzel::fixture {

struct Recorder {
    std::vector<int> pending_ops; // the offence
};

} // namespace heinzel::fixture
