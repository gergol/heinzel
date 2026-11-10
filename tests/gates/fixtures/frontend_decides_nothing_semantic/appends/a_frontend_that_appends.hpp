#pragma once

// NOT an offender: the frontend_decides_nothing_semantic gate must pass this file. Code that says "appending" or
// "depending" names no deferred operation, and a gate that tripped on them would be read past.
namespace heinzel::fixture {

struct Builder {
    int appending_to{0};
    int depending_on{0};
};

} // namespace heinzel::fixture
