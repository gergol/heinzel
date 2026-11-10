#pragma once

// Shim — the `gradients` capture spelling now lives in the exported application-layer header
// `app/heinzel/gradients.hpp` (Gerald's Q2 = option 1 packaging decision, 2026-09-06 08:45). This
// header stays as the include path the already-built consumers (fir_fit, mlp_train, tutorial 07,
// frontend_gradients) were written against; it forwards to the one exported definition so there is a
// single source of truth and no boundary is relaxed.

#include "heinzel/gradients.hpp"
