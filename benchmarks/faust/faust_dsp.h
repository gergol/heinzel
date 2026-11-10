// Minimal FAUST runtime base — vendored so the committed generated C++ compiles with NO faust
// headers installed (the whole point of committing the output: any host / CI can build the bench).
// This is the small, stable interface every faust-generated `class X : public dsp` expects: the
// `dsp` base, the `UI`/`Meta` visitor stubs, and FAUSTFLOAT. The method set mirrors the generated
// classes (getNumInputs/Outputs, init/instanceInit/instanceConstants/instanceClear/
// instanceResetUserInterface, getSampleRate, buildUserInterface, metadata, clone, compute) — see the
// preamble of any benchmarks/faust/generated/*.cpp. The FAUST compiler grants a license exception
// for its output; this hand-written interface header is ours. See Plan/archive/20.
//
// Force-included into each generated TU via the CMake compile flag `-include .../faust/faust_dsp.h`
// (so the generated files stay byte-for-byte the raw faust output + one appended factory line).
#ifndef HEINZEL_FAUST_DSP_H
#define HEINZEL_FAUST_DSP_H

#include <cstdint>

#ifndef FAUSTFLOAT
#define FAUSTFLOAT float
#endif

struct Soundfile; // referenced only by UI::addSoundfile; never used by these DSPs

// UI visitor — every method a no-op (the bench drives compute() directly, no GUI/controls).
struct UI {
    virtual ~UI() = default;
    virtual void openTabBox(const char*) {}
    virtual void openHorizontalBox(const char*) {}
    virtual void openVerticalBox(const char*) {}
    virtual void closeBox() {}
    virtual void addButton(const char*, FAUSTFLOAT*) {}
    virtual void addCheckButton(const char*, FAUSTFLOAT*) {}
    virtual void addVerticalSlider(const char*, FAUSTFLOAT*, FAUSTFLOAT, FAUSTFLOAT, FAUSTFLOAT, FAUSTFLOAT) {}
    virtual void addHorizontalSlider(const char*, FAUSTFLOAT*, FAUSTFLOAT, FAUSTFLOAT, FAUSTFLOAT, FAUSTFLOAT) {}
    virtual void addNumEntry(const char*, FAUSTFLOAT*, FAUSTFLOAT, FAUSTFLOAT, FAUSTFLOAT, FAUSTFLOAT) {}
    virtual void addHorizontalBargraph(const char*, FAUSTFLOAT*, FAUSTFLOAT, FAUSTFLOAT) {}
    virtual void addVerticalBargraph(const char*, FAUSTFLOAT*, FAUSTFLOAT, FAUSTFLOAT) {}
    virtual void addSoundfile(const char*, const char*, Soundfile**) {}
    virtual void declare(FAUSTFLOAT*, const char*, const char*) {}
};

struct Meta {
    virtual ~Meta() = default;
    virtual void declare(const char*, const char*) {}
};

// The dsp base the generated class derives from. Non-pure (empty bodies) so a generated class that
// happens not to emit one of these still links; every method we actually call (init, compute) is
// always emitted and overrides here.
struct dsp {
    virtual ~dsp() = default;
    virtual int getNumInputs() { return 0; }
    virtual int getNumOutputs() { return 0; }
    virtual void buildUserInterface(UI*) {}
    virtual int getSampleRate() { return 0; }
    virtual void init(int) {}
    virtual void instanceInit(int) {}
    virtual void instanceConstants(int) {}
    virtual void instanceResetUserInterface() {}
    virtual void instanceClear() {}
    virtual dsp* clone() { return nullptr; }
    virtual void metadata(Meta*) {}
    virtual void compute(int, FAUSTFLOAT**, FAUSTFLOAT**) {}
};

#endif // HEINZEL_FAUST_DSP_H
