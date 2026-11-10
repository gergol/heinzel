/* ------------------------------------------------------------
name: "sine"
Code generated with Faust 2.85.5 (https://faust.grame.fr)
Compilation options: -lang cpp -fpga-mem-th 4 -light -ct 1 -cn heinzel_sine_vec -es 1 -mcd 16 -mdd 1024 -mdy 33 -single -ftz 0 -vec -lv 0 -vs 32
------------------------------------------------------------ */

#ifndef  __heinzel_sine_vec_H__
#define  __heinzel_sine_vec_H__

#ifndef FAUSTFLOAT
#define FAUSTFLOAT float
#endif 

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <math.h>

#ifndef FAUSTCLASS 
#define FAUSTCLASS heinzel_sine_vec
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

class heinzel_sine_vecSIG0 {
	
  private:
	
	int iVec1[2];
	int iRec1[2];
	int fSampleRate;
	
  public:
	
	int getNumInputsheinzel_sine_vecSIG0() {
		return 0;
	}
	int getNumOutputsheinzel_sine_vecSIG0() {
		return 1;
	}
	
	void instanceInitheinzel_sine_vecSIG0(int sample_rate) {
		fSampleRate = sample_rate;
		for (int l2 = 0; l2 < 2; l2 = l2 + 1) {
			iVec1[l2] = 0;
		}
		for (int l3 = 0; l3 < 2; l3 = l3 + 1) {
			iRec1[l3] = 0;
		}
	}
	
	void fillheinzel_sine_vecSIG0(int count, float* table) {
		for (int i1 = 0; i1 < count; i1 = i1 + 1) {
			iVec1[0] = 1;
			iRec1[0] = (iVec1[1] + iRec1[1]) % 65536;
			table[i1] = std::sin(9.58738e-05f * static_cast<float>(iRec1[0]));
			iVec1[1] = iVec1[0];
			iRec1[1] = iRec1[0];
		}
	}

};

static heinzel_sine_vecSIG0* newheinzel_sine_vecSIG0() { return (heinzel_sine_vecSIG0*)new heinzel_sine_vecSIG0(); }
static void deleteheinzel_sine_vecSIG0(heinzel_sine_vecSIG0* dsp) { delete dsp; }

static float ftbl0heinzel_sine_vecSIG0[65536];

class heinzel_sine_vec : public dsp {
	
 private:
	
	int iVec0_perm[4];
	int fSampleRate;
	float fConst0;
	float fRec0_perm[4];
	
 public:
	heinzel_sine_vec() {
	}
	
	heinzel_sine_vec(const heinzel_sine_vec&) = default;
	
	virtual ~heinzel_sine_vec() = default;
	
	heinzel_sine_vec& operator=(const heinzel_sine_vec&) = default;
	
	void metadata(Meta* m) { 
		m->declare("basics.lib/name", "Faust Basic Element Library");
		m->declare("basics.lib/version", "1.22.0");
		m->declare("compile_options", "-lang cpp -fpga-mem-th 4 -light -ct 1 -cn heinzel_sine_vec -es 1 -mcd 16 -mdd 1024 -mdy 33 -single -ftz 0 -vec -lv 0 -vs 32");
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
		heinzel_sine_vecSIG0* sig0 = newheinzel_sine_vecSIG0();
		sig0->instanceInitheinzel_sine_vecSIG0(sample_rate);
		sig0->fillheinzel_sine_vecSIG0(65536, ftbl0heinzel_sine_vecSIG0);
		deleteheinzel_sine_vecSIG0(sig0);
	}
	
	virtual void instanceConstants(int sample_rate) {
		fSampleRate = sample_rate;
		fConst0 = 4.4e+02f / std::min<float>(1.92e+05f, std::max<float>(1.0f, static_cast<float>(fSampleRate)));
	}
	
	virtual void instanceResetUserInterface() {
	}
	
	virtual void instanceClear() {
		for (int l0 = 0; l0 < 4; l0 = l0 + 1) {
			iVec0_perm[l0] = 0;
		}
		for (int l1 = 0; l1 < 4; l1 = l1 + 1) {
			fRec0_perm[l1] = 0.0f;
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
	
	virtual heinzel_sine_vec* clone() {
		return new heinzel_sine_vec(*this);
	}
	
	virtual int getSampleRate() {
		return fSampleRate;
	}
	
	virtual void buildUserInterface(UI* ui_interface) {
		ui_interface->openVerticalBox("sine");
		ui_interface->closeBox();
	}
	
	virtual void compute(int count, FAUSTFLOAT** RESTRICT inputs, FAUSTFLOAT** RESTRICT outputs) {
		FAUSTFLOAT* output0_ptr = outputs[0];
		int iVec0_tmp[36];
		int* iVec0 = &iVec0_tmp[4];
		float fZec0[32];
		float fRec0_tmp[36];
		float* fRec0 = &fRec0_tmp[4];
		int vindex = 0;
		/* Main loop */
		for (vindex = 0; vindex <= (count - 32); vindex = vindex + 32) {
			FAUSTFLOAT* output0 = &output0_ptr[vindex];
			int vsize = 32;
			/* Vectorizable loop 0 */
			/* Pre code */
			for (int j0 = 0; j0 < 4; j0 = j0 + 1) {
				iVec0_tmp[j0] = iVec0_perm[j0];
			}
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				iVec0[i] = 1;
			}
			/* Post code */
			for (int j1 = 0; j1 < 4; j1 = j1 + 1) {
				iVec0_perm[j1] = iVec0_tmp[vsize + j1];
			}
			/* Recursive loop 1 */
			/* Pre code */
			for (int j2 = 0; j2 < 4; j2 = j2 + 1) {
				fRec0_tmp[j2] = fRec0_perm[j2];
			}
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				fZec0[i] = ((1 - iVec0[i - 1]) ? 0.0f : fConst0 + fRec0[i - 1]);
				fRec0[i] = fZec0[i] - std::floor(fZec0[i]);
			}
			/* Post code */
			for (int j3 = 0; j3 < 4; j3 = j3 + 1) {
				fRec0_perm[j3] = fRec0_tmp[vsize + j3];
			}
			/* Vectorizable loop 2 */
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				output0[i] = static_cast<FAUSTFLOAT>(0.5f * ftbl0heinzel_sine_vecSIG0[std::max<int>(0, std::min<int>(static_cast<int>(65536.0f * fRec0[i]), 65535))]);
			}
		}
		/* Remaining frames */
		if (vindex < count) {
			FAUSTFLOAT* output0 = &output0_ptr[vindex];
			int vsize = count - vindex;
			/* Vectorizable loop 0 */
			/* Pre code */
			for (int j0 = 0; j0 < 4; j0 = j0 + 1) {
				iVec0_tmp[j0] = iVec0_perm[j0];
			}
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				iVec0[i] = 1;
			}
			/* Post code */
			for (int j1 = 0; j1 < 4; j1 = j1 + 1) {
				iVec0_perm[j1] = iVec0_tmp[vsize + j1];
			}
			/* Recursive loop 1 */
			/* Pre code */
			for (int j2 = 0; j2 < 4; j2 = j2 + 1) {
				fRec0_tmp[j2] = fRec0_perm[j2];
			}
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				fZec0[i] = ((1 - iVec0[i - 1]) ? 0.0f : fConst0 + fRec0[i - 1]);
				fRec0[i] = fZec0[i] - std::floor(fZec0[i]);
			}
			/* Post code */
			for (int j3 = 0; j3 < 4; j3 = j3 + 1) {
				fRec0_perm[j3] = fRec0_tmp[vsize + j3];
			}
			/* Vectorizable loop 2 */
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				output0[i] = static_cast<FAUSTFLOAT>(0.5f * ftbl0heinzel_sine_vecSIG0[std::max<int>(0, std::min<int>(static_cast<int>(65536.0f * fRec0[i]), 65535))]);
			}
		}
	}

};

#endif

// --- heinzel factory (appended by tools/gen-faust.sh) ---
dsp* heinzel_make_sine_vec() { return new heinzel_sine_vec(); }
