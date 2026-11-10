/* ------------------------------------------------------------
name: "strip"
Code generated with Faust 2.85.5 (https://faust.grame.fr)
Compilation options: -lang cpp -fpga-mem-th 4 -light -ct 1 -cn heinzel_strip_scal -es 1 -mcd 16 -mdd 1024 -mdy 33 -single -ftz 0
------------------------------------------------------------ */

#ifndef  __heinzel_strip_scal_H__
#define  __heinzel_strip_scal_H__

#ifndef FAUSTFLOAT
#define FAUSTFLOAT float
#endif 

/* link with : "" */
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <math.h>

#ifndef FAUSTCLASS 
#define FAUSTCLASS heinzel_strip_scal
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


class heinzel_strip_scal : public dsp {
	
 private:
	
	int fSampleRate;
	
 public:
	heinzel_strip_scal() {
	}
	
	heinzel_strip_scal(const heinzel_strip_scal&) = default;
	
	virtual ~heinzel_strip_scal() = default;
	
	heinzel_strip_scal& operator=(const heinzel_strip_scal&) = default;
	
	void metadata(Meta* m) { 
		m->declare("compile_options", "-lang cpp -fpga-mem-th 4 -light -ct 1 -cn heinzel_strip_scal -es 1 -mcd 16 -mdd 1024 -mdy 33 -single -ftz 0");
		m->declare("filename", "strip.dsp");
		m->declare("maths.lib/author", "GRAME");
		m->declare("maths.lib/copyright", "GRAME");
		m->declare("maths.lib/license", "LGPL with exception");
		m->declare("maths.lib/name", "Faust Math Library");
		m->declare("maths.lib/version", "2.9.0");
		m->declare("name", "strip");
	}

	virtual int getNumInputs() {
		return 1;
	}
	virtual int getNumOutputs() {
		return 1;
	}
	
	static void classInit(int sample_rate) {
	}
	
	virtual void instanceConstants(int sample_rate) {
		fSampleRate = sample_rate;
	}
	
	virtual void instanceResetUserInterface() {
	}
	
	virtual void instanceClear() {
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
	
	virtual heinzel_strip_scal* clone() {
		return new heinzel_strip_scal(*this);
	}
	
	virtual int getSampleRate() {
		return fSampleRate;
	}
	
	virtual void buildUserInterface(UI* ui_interface) {
		ui_interface->openVerticalBox("strip");
		ui_interface->closeBox();
	}
	
	virtual void compute(int count, FAUSTFLOAT** RESTRICT inputs, FAUSTFLOAT** RESTRICT outputs) {
		FAUSTFLOAT* input0 = inputs[0];
		FAUSTFLOAT* output0 = outputs[0];
		for (int i0 = 0; i0 < count; i0 = i0 + 1) {
			output0[i0] = static_cast<FAUSTFLOAT>(std::min<float>(1.0f, std::max<float>(-1.0f, 0.7f * tanhf(1.1f * tanhf(1.3f * tanhf(3.0f * static_cast<float>(input0[i0]) + 0.1f) + -0.05f)))));
		}
	}

};

#endif

// --- heinzel factory (appended by tools/gen-faust.sh) ---
dsp* heinzel_make_strip_scal() { return new heinzel_strip_scal(); }
