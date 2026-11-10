#pragma once

// A DELIBERATE offender for the `reference_no_loops` and `reference_no_schedule` gates. Compiled by
// nothing; it exists only so the gates can be pointed at it and OBSERVED to object. The reference walk is
// what the loop tree is checked against, so a reference header that runs part of the loop tree, or reads
// the machine model the planner plans from, is exactly the include the gates forbid. The lines the
// includes sit on are pinned by the would-object tests' regular expressions.

#include "heinzel/ir/ir.hpp"
#include "heinzel/loops/run.hpp"
#include "heinzel/schedule/machine.hpp"
