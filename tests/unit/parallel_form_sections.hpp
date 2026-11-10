// The second-order sections of rewrite_parallel_form_test, as float coefficients (a data table).
#pragma once

#include <array>

namespace {

struct Section {
    // y = b0 x + b1 x[n-1] + b2 x[n-2] - a1 y[n-1] - a2 y[n-2]
    float b0;
    float b1;
    float b2;
    float a1;
    float a2;
};

// KFR's EQ and graphic EQ and klang's three sections (`benchmarks/iir_arms.hpp`), `dsp/cascade4`
// (`benchmarks/bench_apps_dsp.cpp`), Butterworth low-passes of order 8 and 10 at 30 Hz, a Butterworth high-pass of
// order 8 at 5 Hz and a 4th-order Linkwitz-Riley low-pass at 2 kHz (RBJ, 48 kHz), rounded to float.
constexpr std::array<Section, 3> kEq3{{
    {0x1.fc38e2p-1f, -0x1.fc38e2p+0f, 0x1.fc38e2p-1f, -0x1.fc355p+0f, 0x1.f878e4p-1f},
    {0x1.0b4082p+0f, -0x1.e533bcp+0f, 0x1.bc4618p-1f, -0x1.e533bcp+0f, 0x1.d2c71cp-1f},
    {0x1.3d86bap-3f, 0x1.3d86bap-2f, 0x1.3d86bap-3f, -0x1.3d86bap-1f, 0x1.ec35d2p-3f},
}};
constexpr std::array<Section, 8> kEq8{{
    {0x1.fe1b8ap-1f, -0x1.fe1b8ap+0f, 0x1.fe1b8ap-1f, -0x1.fe1aa4p+0f, 0x1.fc38dep-1f},
    {0x1.00c51p+0f, -0x1.fc349ap+0f, 0x1.f6ff3p-1f, -0x1.fc349ap+0f, 0x1.f8894ep-1f},
    {0x1.fcd68p-1f, -0x1.f4e78cp+0f, 0x1.ed81fcp-1f, -0x1.f4e78cp+0f, 0x1.ea587ap-1f},
    {0x1.032206p+0f, -0x1.efbf68p+0f, 0x1.db5b6p-1f, -0x1.efbf68p+0f, 0x1.e19f7p-1f},
    {0x1.f422a6p-1f, -0x1.d357aep+0f, 0x1.ba9d6ep-1f, -0x1.d357aep+0f, 0x1.aec014p-1f},
    {0x1.0b65e2p+0f, -0x1.b92dcep+0f, 0x1.7ab028p-1f, -0x1.b92dcep+0f, 0x1.917beep-1f},
    {0x1.dae2e8p-1f, -0x1.4d5a2cp+0f, 0x1.26f52cp-1f, -0x1.4d5a2cp+0f, 0x1.01d814p-1f},
    {0x1.2be764p-2f, 0x1.2be764p-1f, 0x1.2be764p-2f, 0x1.b7df4cp-25f, 0x1.5f3b2cp-3f},
}};
constexpr std::array<Section, 4> kCascade4{{{0.3f, 0.2f, 0, 0.4f, -0.1f}, {0.5f, 0.1f, 0, -0.3f, 0.2f}, {0.4f, 0.2f, 0, 0.1f, -0.15f}, {0.6f, 0.1f, 0, -0.2f, 0.1f}}};
constexpr std::array<Section, 4> kButterworth8{{
    {0x1.01bb96p-18f, 0x1.01bb96p-17f, 0x1.01bb96p-18f, -0x1.fe081ap+0f, 0x1.fc1238p-1f},
    {0x1.01e226p-18f, 0x1.01e226p-17f, 0x1.01e226p-18f, -0x1.fe546ap+0f, 0x1.fcaad8p-1f},
    {0x1.022986p-18f, 0x1.022986p-17f, 0x1.022986p-18f, -0x1.fee1a8p+0f, 0x1.fdc552p-1f},
    {0x1.028702p-18f, 0x1.028702p-17f, 0x1.028702p-18f, -0x1.ff9aa6p+0f, 0x1.ff3752p-1f},
}};
constexpr std::array<Section, 5> kButterworth10{{
    {0x1.01b9cep-18f, 0x1.01b9cep-17f, 0x1.01b9cep-18f, -0x1.fe0494p+0f, 0x1.fc0b2cp-1f},
    {0x1.01d2c4p-18f, 0x1.01d2c4p-17f, 0x1.01d2c4p-18f, -0x1.fe35fap+0f, 0x1.fc6df8p-1f},
    {0x1.02024cp-18f, 0x1.02024cp-17f, 0x1.02024cp-18f, -0x1.fe940ap+0f, 0x1.fd2a18p-1f},
    {0x1.0243d6p-18f, 0x1.0243d6p-17f, 0x1.0243d6p-18f, -0x1.ff15bap+0f, 0x1.fe2d7ap-1f},
    {0x1.02910cp-18f, 0x1.02910cp-17f, 0x1.02910cp-18f, -0x1.ffae86p+0f, 0x1.ff5f1p-1f},
}};
constexpr std::array<Section, 4> kHighPassAt5{{
    {0x1.ffabe6p-1f, -0x1.ffabe6p+0f, 0x1.ffabe6p-1f, -0x1.ffabe4p+0f, 0x1.ff57d4p-1f},
    {0x1.ffb8b2p-1f, -0x1.ffb8b2p+0f, 0x1.ffb8b2p-1f, -0x1.ffb8aep+0f, 0x1.ff716cp-1f},
    {0x1.ffd058p-1f, -0x1.ffd058p+0f, 0x1.ffd058p-1f, -0x1.ffd054p+0f, 0x1.ffa0b6p-1f},
    {0x1.ffef4p-1f, -0x1.ffef4p+0f, 0x1.ffef4p-1f, -0x1.ffef3cp+0f, 0x1.ffde88p-1f},
}};
constexpr std::array<Section, 3> kKlang3{{
    {0x1.fc3906p-1f, -0x1.fc3906p+0f, 0x1.fc3906p-1f, -0x1.fc3574p+0f, 0x1.f8792cp-1f},
    {0x1.3d8b64p-3f, 0x1.3d8b64p-2f, 0x1.3d8b64p-3f, -0x1.3d8b64p-1f, 0x1.ec5b22p-3f},
    {0x1.eac6acp-6f, 0x1.eac6acp-5f, 0x1.eac6acp-6f, -0x1.74494ep+0f, 0x1.25eb72p-1f},
}};
constexpr Section kLinkwitzRiley{0x1.d7e80ap-7f, 0x1.d7e80ap-6f, 0x1.d7e80ap-7f, -0x1.a20bd8p+0f, 0x1.61962ep-1f};

} // namespace
