// Linux/Clang port shim for nashaudio/klang's klang.h.
//
// klang.h only defines its platform bits for wasm / Apple / Windows, and relies on a few names
// being visible in the global namespace that libstdc++ scopes under std::. Rather than patch the
// fetched header, we set that environment up here and pull klang.h in behind it. The comparison
// bench includes THIS header instead of <klang.h> directly.
//
// (This makes the header build under Clang 22. GCC 15 still rejects klang.h outright — an anonymous
// union with constructor-bearing `signal` members is ill-formed and GCC enforces it — which is why
// the klang bench target is Clang-only.)
#pragma once

// 1. Platform macros klang.h leaves undefined outside wasm/Apple/Windows (mirrors its Windows branch).
#ifndef THREAD_LOCAL
#define THREAD_LOCAL thread_local
#endif
#ifndef SQRT
#define SQRT ::sqrt
#endif
#ifndef SQRTF
#define SQRTF ::sqrtf
#endif
#ifndef ABS
#define ABS ::abs
#endif
#ifndef FABS
#define FABS ::fabsf
#endif

// 2. klang.h uses unqualified memset/memcpy but doesn't include the header for them on this platform.
#include <cstring>

// 3. klang.h references ::isnan / ::isinf; libstdc++ only provides std::isnan/std::isinf. A global-scope
//    using-declaration puts them in the global namespace so those qualified calls resolve.
#include <cmath>
using std::isinf;
using std::isnan;

#include <klang.h>

namespace hz = heinzel;

// klang.h defines object-like macros `abs`/`sqrt` (-> klang::abs/klang::sqrt) that leak into the rest
// of the TU and hijack even qualified names like hz::abs. klang's own (already-parsed) inline code keeps
// them; undo them here so the including bench sees clean identifiers.
#undef abs
#undef sqrt
