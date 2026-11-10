// The planner's cost model (`schedule/cost.hpp`, Plan/23 04): what an operation costs by its rule-table class, and the
// order the model puts measured stages in. The points are the corpus's (`Plan/probes/audio-iir/cost_corpus*.cpp`), each
// stage's counts as the probe read them off the lowered program, and each pair ordered as zen2 ran it: the model must
// pick the faster of the two, which is all the planner asks of it.

#include "heinzel/schedule/cost.hpp"

using namespace heinzel;

namespace {

constexpr double cost_of(std::initializer_list<cost::Stage> stages) {
    double sum = 0;
    for (const cost::Stage& s : stages) {
        sum += cost::cycles(s, zen2);
    }
    return sum;
}

// A float's eight lanes along time: the feed-forward part before a cascade's first cycle.
constexpr cost::Stage kFeed{.depth = 6, .slots = 3, .loads = 2, .stores = 1, .lanes = 8};

} // namespace

// What an operation costs: its class's figures, a division or a root in double dearer than in float.
static_assert(cost::of(op::Add, ElemType::F32, zen2) == OpCost{3, 1} && cost::of(op::MultiplyAdd, ElemType::F32, zen2) == OpCost{5, 1},
              "an add, a multiply-add: the machine's figures");
static_assert(cost::of(op::Div, ElemType::F64, zen2).latency > cost::of(op::Div, ElemType::F32, zen2).latency &&
                  cost::of(op::Sqrt, ElemType::F64, zen2).latency > cost::of(op::Sqrt, ElemType::F32, zen2).latency,
              "a division and a root take longer in double");
static_assert(cost::of(op::FixedDelay, ElemType::F32, zen2) == OpCost{0, 0}, "a lag computes nothing");

// Eight biquad sections as written, one feedback stage against two of four: one sample's path, 93 cycles deep, outgrows
// the out-of-order window (measured 38.7 cycles a sample against 29.5, `cost_corpus4.cpp`).
static_assert(cost_of({kFeed, {.chain = 9, .depth = 93, .slots = 53, .loads = 9, .stores = 8}}) >
                  cost_of({kFeed, {.chain = 9, .depth = 45, .slots = 25, .loads = 5, .stores = 5}, {.chain = 9, .depth = 48, .slots = 28, .loads = 6, .stores = 4}}),
              "eight sections as written: two stages beat one");
// ...and rewritten (ordered and fused, half as deep) one stage beats two (22.3 against 26.1).
static_assert(cost_of({kFeed, {.chain = 11, .depth = 46, .slots = 45, .loads = 9, .stores = 9}}) <
                  cost_of({kFeed, {.chain = 11, .depth = 26, .slots = 21, .loads = 5, .stores = 5}, {.chain = 11, .depth = 29, .slots = 24, .loads = 6, .stores = 5}}),
              "eight sections rewritten: one stage beats two");
// Two biquads with 20 maps between them: apart beats merged (20.6 against 34.4, `cost_corpus.cpp`); with 8, merged.
static_assert(cost_of({kFeed, {.chain = 9, .depth = 81, .slots = 31, .loads = 1, .stores = 2}}) >
                  cost_of({kFeed, {.chain = 9, .depth = 9, .slots = 4, .loads = 1, .stores = 1}, {.depth = 63, .slots = 21, .loads = 1, .stores = 2, .lanes = 8},
                           {.depth = 6, .slots = 2, .loads = 2, .stores = 1, .lanes = 8}, {.chain = 9, .depth = 9, .slots = 4, .loads = 1, .stores = 1}}),
              "two biquads twenty maps apart: the maps in lanes, the cycles apart");
// Work beside a chain where the two bounds meet (`cost_corpus7.cpp`): two maps each reading a biquad's value cost it 1.4
// cycles a sample joined, more than their own stage in lanes (7.6 against 6.2); one map after two sections hides under
// their chain (11.0 against 11.8).
static_assert(cost_of({{.chain = 5.5, .depth = 23, .slots = 11, .loads = 2, .stores = 1}}) >
                  cost_of({{.chain = 5.5, .depth = 14, .slots = 6, .loads = 2, .stores = 1}, {.depth = 9, .slots = 5, .loads = 1, .stores = 1, .lanes = 8}}),
              "two maps beside a biquad: apart");
static_assert(cost_of({{.chain = 11, .depth = 25, .slots = 14, .loads = 2, .stores = 1}}) <
                  cost_of({{.chain = 11, .depth = 19, .slots = 12, .loads = 2, .stores = 1}, {.depth = 6, .slots = 2, .loads = 1, .stores = 1, .lanes = 8}}),
              "a map after two sections: joined");
// The two bounds together: where one dwarfs the other, it; where they meet, more than either, by 2^(1/8) on zen2.
static_assert(cost::busy(6, 0, zen2) == 6 && cost::busy(0, 6, zen2) == 6 && cost::busy(6, 6, zen2) > 6.54 && cost::busy(6, 6, zen2) < 6.55 &&
                  cost::busy(6, 3, zen2) < 6.01 && cost::busy(6, 6, Machine{.contention = 0}) == 6,
              "a chain and an issue bound, taken together");
// A division on a one-voice chain in double: 24 cycles against 21 in float (`cost_corpus2.cpp`).
static_assert(cost::cycles({.chain = 24, .depth = 24, .slots = 14, .loads = 1, .stores = 1}, zen2) >
                  cost::cycles({.chain = 21, .depth = 21, .slots = 11, .loads = 1, .stores = 1}, zen2),
              "a chain through a division is longer in double");

// eq8mix's direct term, a double: beside its gain, stored and loaded back by the sum's stage, or in that stage
// (`cost_corpus16.cpp`, 4.3f). Over 1M samples the stored buffers go through memory: 15.8 cycles a sample against 13.1;
// in blocks, in the caches, 13.1 against 12.5.
constexpr cost::Stage kGainStored{.slots = 2, .loads = 1, .stores = 2, .lanes = 4, .bytes = 16};
constexpr cost::Stage kSumStored{.slots = 6, .loads = 3, .stores = 1, .lanes = 4, .bytes = 16};
constexpr cost::Stage kGainAlone{.slots = 1, .loads = 1, .stores = 1, .lanes = 8, .bytes = 8};
constexpr cost::Stage kSumWithIt{.slots = 7, .loads = 3, .stores = 1, .lanes = 4, .bytes = 12};
static_assert(cost_of({kGainStored, kSumStored}) > cost_of({kGainAlone, kSumWithIt}), "a double read past the next stage: in its reader, whole");
static_assert(cost::cycles({.slots = 2, .loads = 1, .stores = 2, .lanes = 4}, zen2) + cost::cycles({.slots = 6, .loads = 3, .stores = 1, .lanes = 4}, zen2) >
                  cost::cycles({.slots = 1, .loads = 1, .stores = 1, .lanes = 8}, zen2) + cost::cycles({.slots = 7, .loads = 3, .stores = 1, .lanes = 4}, zen2),
              "...and in the caches");
// What a stage moves through memory bounds it: 16 bytes a sample at zen2's 4 a cycle, whatever it computes.
static_assert(cost::cycles({.slots = 2, .lanes = 8, .bytes = 16}, zen2) == 4 && cost::cycles({.slots = 2, .lanes = 8, .bytes = 16}, Machine{.memory_bytes = 0}) < 1,
              "the memory bound");

int main() { return 0; }
