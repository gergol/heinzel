/* ------------------------------------------------------------
name: "sine"
Code generated with Faust 2.85.5 (https://faust.grame.fr)
Compilation options: -lang cpp -fpga-mem-th 4 -light -ct 1 -cn heinzel_sine_scal -es 1 -mcd 16 -mdd 1024 -mdy 33 -single -ftz 0
------------------------------------------------------------ */

#ifndef  __heinzel_sine_scal_H__
#define  __heinzel_sine_scal_H__

#ifndef FAUSTFLOAT
#define FAUSTFLOAT float
#endif 

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <math.h>

#ifndef FAUSTCLASS 
#define FAUSTCLASS heinzel_sine_scal
#endif

#ifdef __APPLE__ 
#define exp10f __exp10f
#define exp10 __exp10
#endif

#if defined(_WIN32)
#define RESTRICT __restrict
#else
#define RESTRICT __restrict__
#endif

class heinzel_sine_scalSIG0 {
	
  private:
	
	int iVec0[2];
	int iRec0[2];
	int fSampleRate;
	
  public:
	
	int getNumInputsheinzel_sine_scalSIG0() {
		return 0;
	}
	int getNumOutputsheinzel_sine_scalSIG0() {
		return 1;
	}
	
	void instanceInitheinzel_sine_scalSIG0(int sample_rate) {
		fSampleRate = sample_rate;
		for (int l0 = 0; l0 < 2; l0 = l0 + 1) {
			iVec0[l0] = 0;
		}
		for (int l1 = 0; l1 < 2; l1 = l1 + 1) {
			iRec0[l1] = 0;
		}
	}
	
	void fillheinzel_sine_scalSIG0(int count, float* table) {
		for (int i1 = 0; i1 < count; i1 = i1 + 1) {
			iVec0[0] = 1;
			iRec0[0] = (iVec0[1] + iRec0[1]) % 65536;
			table[i1] = std::sin(9.58738e-05f * static_cast<float>(iRec0[0]));
			iVec0[1] = iVec0[0];
			iRec0[1] = iRec0[0];
		}
	}

};

static heinzel_sine_scalSIG0* newheinzel_sine_scalSIG0() { return (heinzel_sine_scalSIG0*)new heinzel_sine_scalSIG0(); }
static void deleteheinzel_sine_scalSIG0(heinzel_sine_scalSIG0* dsp) { delete dsp; }

static float ftbl0heinzel_sine_scalSIG0[65536];

class heinzel_sine_scal : public dsp {
	
 private:
	
	int iVec1[2];
	int fSampleRate;
	float fConst0;
	float fRec1[2];
	
 public:
	heinzel_sine_scal() {
	}
	
	heinzel_sine_scal(const heinzel_sine_scal&) = default;
	
	virtual ~heinzel_sine_scal() = default;
	
	heinzel_sine_scal& operator=(const heinzel_sine_scal&) = default;
	
	void metadata(Meta* m) { 
		m->declare("basics.lib/name", "Faust Basic Element Library");
		m->declare("basics.lib/version", "1.22.0");
		m->declare("compile_options", "-lang cpp -fpga-mem-th 4 -light -ct 1 -cn heinzel_sine_scal -es 1 -mcd 16 -mdd 1024 -mdy 33 -single -ftz 0");
		m->declare("filename", "sine.dsp");
		m->declare("maths.lib/author", "GRAME");
		m->declare("maths.lib/copyright", "GRAME");
		m->declare("maths.lib/license", "LGPL with exception");
		m->declare("maths.lib/name", "Faust Math Library");
		m->declare("maths.lib/version", "2.9.0");
		m->declare("name", "sine");
		m->declare("oscillators.lib/name", "Faust Oscillator Library");
		m->declare("oscillators.lib/version", "1.7.0");
		m->declare("platform.lib/name", "Generic Platform Library");
		m->declare("platform.lib/version", "1.3.0");
	}

	virtual int getNumInputs() {
		return 0;
	}
	virtual int getNumOutputs() {
		return 1;
	}
	
	static void classInit(int sample_rate) {
		heinzel_sine_scalSIG0* sig0 = newheinzel_sine_scalSIG0();
		sig0->instanceInitheinzel_sine_scalSIG0(sample_rate);
		sig0->fillheinzel_sine_scalSIG0(65536, ftbl0heinzel_sine_scalSIG0);
		deleteheinzel_sine_scalSIG0(sig0);
	}
	
	virtual void instanceConstants(int sample_rate) {
		fSampleRate = sample_rate;
		fConst0 = 4.4e+02f / std::min<float>(1.92e+05f, std::max<float>(1.0f, static_cast<float>(fSampleRate)));
	}
	
	virtual void instanceResetUserInterface() {
	}
	
	virtual void instanceClear() {
		for (int l2 = 0; l2 < 2; l2 = l2 + 1) {
			iVec1[l2] = 0;
		}
		for (int l3 = 0; l3 < 2; l3 = l3 + 1) {
			fRec1[l3] = 0.0f;
		}
	}
	
	virtual void init(int sample_rate) {
		classInit(sample_rate);
		instanceInit(sample_rate);
	}
	
	virtual void instanceInit(int sample_rate) {
		instanceConstants(sample_rate);
		instanceResetUserInterface();
		instanceClear();
	}
	
	virtual heinzel_sine_scal* clone() {
		return new heinzel_sine_scal(*this);
	}
	
	virtual int getSampleRate() {
		return fSampleRate;
	}
	
	virtual void buildUserInterface(UI* ui_interface) {
		ui_interface->openVerticalBox("sine");
		ui_interface->closeBox();
	}
	
	virtual void compute(int count, FAUSTFLOAT** RESTRICT inputs, FAUSTFLOAT** RESTRICT outputs) {
		FAUSTFLOAT* output0 = outputs[0];
		for (int i0 = 0; i0 < count; i0 = i0 + 1) {
			iVec1[0] = 1;
			float fTemp0 = ((1 - iVec1[1]) ? 0.0f : fConst0 + fRec1[1]);
			fRec1[0] = fTemp0 - std::floor(fTemp0);
			output0[i0] = static_cast<FAUSTFLOAT>(0.5f * ftbl0heinzel_sine_scalSIG0[std::max<int>(0, std::min<int>(static_cast<int>(65536.0f * fRec1[0]), 65535))]);
			iVec1[1] = iVec1[0];
			fRec1[1] = fRec1[0];
		}
	}

};

#endif

// --- heinzel factory (appended by tools/gen-faust.sh) ---
dsp* heinzel_make_sine_scal() { return new heinzel_sine_scal(); }
