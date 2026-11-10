#pragma once

// heinzel/dsl.hpp: the DSL in one include: the declarations and index notation, the members, `einsum`,
// `rearrange` and `reduce`, subgraphs, gradients, and `Executor` to run what they build. It lives in `app/`
// beside `gradients.hpp` for the same reason: it brings together what the core's gates keep apart (the
// frontend and `Executor`, the frontend and autodiff). Link `heinzel_app`.

#include "heinzel/executor.hpp"
#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/frontend/subgraph.hpp"
#include "heinzel/gradients.hpp"
#include "heinzel/notation/einsum.hpp"
#include "heinzel/notation/rearrange.hpp"
