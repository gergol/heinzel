// crv's arms built again with `-mllvm -unroll-threshold=4000` (benchmarks/CMakeLists.txt), named apart: the
// `crv-unrolled-*` arms of bench_compare_crv (bench_compare_crv_heinzel.cpp says why).

#define CRV_ARM(name) name##_unrolled
#include "bench_compare_crv.cpp"
