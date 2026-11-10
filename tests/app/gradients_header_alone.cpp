// `heinzel/gradients.hpp` compiles with heinzel_app's include path and nothing else: no test folder is on
// it, so a hidden dependence on test scaffolding stops the build. The exported header's whole check is
// that this file compiles; `gradients_export_test.cpp` runs what it builds.
#include "heinzel/gradients.hpp"
