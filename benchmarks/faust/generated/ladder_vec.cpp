/* ------------------------------------------------------------
name: "ladder"
Code generated with Faust 2.85.5 (https://faust.grame.fr)
Compilation options: -lang cpp -fpga-mem-th 4 -light -ct 1 -cn heinzel_ladder_vec -es 1 -mcd 16 -mdd 1024 -mdy 33 -single -ftz 0 -vec -lv 0 -vs 32
------------------------------------------------------------ */

#ifndef  __heinzel_ladder_vec_H__
#define  __heinzel_ladder_vec_H__

#ifndef FAUSTFLOAT
#define FAUSTFLOAT float
#endif 

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <math.h>

#ifndef FAUSTCLASS 
#define FAUSTCLASS heinzel_ladder_vec
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

static float heinzel_ladder_vec_faustpower4_f(float value) {
	return value * value * value * value;
}

class heinzel_ladder_vec : public dsp {
	
 private:
	
	int fSampleRate;
	float fConst0;
	float fConst1;
	float fRec4_perm[4];
	float fRec3_perm[4];
	float fRec2_perm[4];
	float fRec1_perm[4];
	float fConst2;
	float fRec0_perm[4];
	
 public:
	heinzel_ladder_vec() {
	}
	
	heinzel_ladder_vec(const heinzel_ladder_vec&) = default;
	
	virtual ~heinzel_ladder_vec() = default;
	
	heinzel_ladder_vec& operator=(const heinzel_ladder_vec&) = default;
	
	void metadata(Meta* m) { 
		m->declare("compile_options", "-lang cpp -fpga-mem-th 4 -light -ct 1 -cn heinzel_ladder_vec -es 1 -mcd 16 -mdd 1024 -mdy 33 -single -ftz 0 -vec -lv 0 -vs 32");
		m->declare("filename", "ladder.dsp");
		m->declare("filters.lib/lowpass0_highpass1", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/name", "Faust Filters Library");
		m->declare("filters.lib/pole:author", "Julius O. Smith III");
		m->declare("filters.lib/pole:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/pole:license", "MIT-style STK-4.3 license");
		m->declare("filters.lib/version", "1.7.1");
		m->declare("maths.lib/author", "GRAME");
		m->declare("maths.lib/copyright", "GRAME");
		m->declare("maths.lib/license", "LGPL with exception");
		m->declare("maths.lib/name", "Faust Math Library");
		m->declare("maths.lib/version", "2.9.0");
		m->declare("name", "ladder");
		m->declare("platform.lib/name", "Generic Platform Library");
		m->declare("platform.lib/version", "1.3.0");
		m->declare("vaeffects.lib/moog_vcf:author", "Julius O. Smith III");
		m->declare("vaeffects.lib/moog_vcf:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("vaeffects.lib/moog_vcf:license", "MIT-style STK-4.3 license");
		m->declare("vaeffects.lib/name", "Faust Virtual Analog Filter Effect Library");
		m->declare("vaeffects.lib/version", "1.5.0");
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
		fConst0 = 6283.1855f / std::min<float>(1.92e+05f, std::max<float>(1.0f, static_cast<float>(fSampleRate)));
		fConst1 = 1.0f - fConst0;
		fConst2 = heinzel_ladder_vec_faustpower4_f(fConst0);
	}
	
	virtual void instanceResetUserInterface() {
	}
	
	virtual void instanceClear() {
		for (int l0 = 0; l0 < 4; l0 = l0 + 1) {
			fRec4_perm[l0] = 0.0f;
		}
		for (int l1 = 0; l1 < 4; l1 = l1 + 1) {
			fRec3_perm[l1] = 0.0f;
		}
		for (int l2 = 0; l2 < 4; l2 = l2 + 1) {
			fRec2_perm[l2] = 0.0f;
		}
		for (int l3 = 0; l3 < 4; l3 = l3 + 1) {
			fRec1_perm[l3] = 0.0f;
		}
		for (int l4 = 0; l4 < 4; l4 = l4 + 1) {
			fRec0_perm[l4] = 0.0f;
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
	
	virtual heinzel_ladder_vec* clone() {
		return new heinzel_ladder_vec(*this);
	}
	
	virtual int getSampleRate() {
		return fSampleRate;
	}
	
	virtual void buildUserInterface(UI* ui_interface) {
		ui_interface->openVerticalBox("ladder");
		ui_interface->closeBox();
	}
	
	virtual void compute(int count, FAUSTFLOAT** RESTRICT inputs, FAUSTFLOAT** RESTRICT outputs) {
		FAUSTFLOAT* input0_ptr = inputs[0];
		FAUSTFLOAT* output0_ptr = outputs[0];
		float fRec4_tmp[36];
		float* fRec4 = &fRec4_tmp[4];
		float fRec3_tmp[36];
		float* fRec3 = &fRec3_tmp[4];
		float fRec2_tmp[36];
		float* fRec2 = &fRec2_tmp[4];
		float fRec1_tmp[36];
		float* fRec1 = &fRec1_tmp[4];
		float fRec0_tmp[36];
		float* fRec0 = &fRec0_tmp[4];
		int vindex = 0;
		/* Main loop */
		for (vindex = 0; vindex <= (count - 32); vindex = vindex + 32) {
			FAUSTFLOAT* input0 = &input0_ptr[vindex];
			FAUSTFLOAT* output0 = &output0_ptr[vindex];
			int vsize = 32;
			/* Recursive loop 0 */
			/* Pre code */
			for (int j0 = 0; j0 < 4; j0 = j0 + 1) {
				fRec4_tmp[j0] = fRec4_perm[j0];
			}
			for (int j2 = 0; j2 < 4; j2 = j2 + 1) {
				fRec3_tmp[j2] = fRec3_perm[j2];
			}
			for (int j4 = 0; j4 < 4; j4 = j4 + 1) {
				fRec2_tmp[j4] = fRec2_perm[j4];
			}
			for (int j6 = 0; j6 < 4; j6 = j6 + 1) {
				fRec1_tmp[j6] = fRec1_perm[j6];
			}
			for (int j8 = 0; j8 < 4; j8 = j8 + 1) {
				fRec0_tmp[j8] = fRec0_perm[j8];
			}
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				fRec4[i] = static_cast<float>(input0[i]) + fConst1 * fRec4[i - 1] - 2.8f * fRec0[i - 1];
				fRec3[i] = fRec4[i] + fConst1 * fRec3[i - 1];
				fRec2[i] = fRec3[i] + fConst1 * fRec2[i - 1];
				fRec1[i] = fRec2[i] + fConst1 * fRec1[i - 1];
				fRec0[i] = fConst2 * fRec1[i];
			}
			/* Post code */
			for (int j1 = 0; j1 < 4; j1 = j1 + 1) {
				fRec4_perm[j1] = fRec4_tmp[vsize + j1];
			}
			for (int j3 = 0; j3 < 4; j3 = j3 + 1) {
				fRec3_perm[j3] = fRec3_tmp[vsize + j3];
			}
			for (int j5 = 0; j5 < 4; j5 = j5 + 1) {
				fRec2_perm[j5] = fRec2_tmp[vsize + j5];
			}
			for (int j7 = 0; j7 < 4; j7 = j7 + 1) {
				fRec1_perm[j7] = fRec1_tmp[vsize + j7];
			}
			for (int j9 = 0; j9 < 4; j9 = j9 + 1) {
				fRec0_perm[j9] = fRec0_tmp[vsize + j9];
			}
			/* Vectorizable loop 1 */
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				output0[i] = static_cast<FAUSTFLOAT>(fRec0[i]);
			}
		}
		/* Remaining frames */
		if (vindex < count) {
			FAUSTFLOAT* input0 = &input0_ptr[vindex];
			FAUSTFLOAT* output0 = &output0_ptr[vindex];
			int vsize = count - vindex;
			/* Recursive loop 0 */
			/* Pre code */
			for (int j0 = 0; j0 < 4; j0 = j0 + 1) {
				fRec4_tmp[j0] = fRec4_perm[j0];
			}
			for (int j2 = 0; j2 < 4; j2 = j2 + 1) {
				fRec3_tmp[j2] = fRec3_perm[j2];
			}
			for (int j4 = 0; j4 < 4; j4 = j4 + 1) {
				fRec2_tmp[j4] = fRec2_perm[j4];
			}
			for (int j6 = 0; j6 < 4; j6 = j6 + 1) {
				fRec1_tmp[j6] = fRec1_perm[j6];
			}
			for (int j8 = 0; j8 < 4; j8 = j8 + 1) {
				fRec0_tmp[j8] = fRec0_perm[j8];
			}
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				fRec4[i] = static_cast<float>(input0[i]) + fConst1 * fRec4[i - 1] - 2.8f * fRec0[i - 1];
				fRec3[i] = fRec4[i] + fConst1 * fRec3[i - 1];
				fRec2[i] = fRec3[i] + fConst1 * fRec2[i - 1];
				fRec1[i] = fRec2[i] + fConst1 * fRec1[i - 1];
				fRec0[i] = fConst2 * fRec1[i];
			}
			/* Post code */
			for (int j1 = 0; j1 < 4; j1 = j1 + 1) {
				fRec4_perm[j1] = fRec4_tmp[vsize + j1];
			}
			for (int j3 = 0; j3 < 4; j3 = j3 + 1) {
				fRec3_perm[j3] = fRec3_tmp[vsize + j3];
			}
			for (int j5 = 0; j5 < 4; j5 = j5 + 1) {
				fRec2_perm[j5] = fRec2_tmp[vsize + j5];
			}
			for (int j7 = 0; j7 < 4; j7 = j7 + 1) {
				fRec1_perm[j7] = fRec1_tmp[vsize + j7];
			}
			for (int j9 = 0; j9 < 4; j9 = j9 + 1) {
				fRec0_perm[j9] = fRec0_tmp[vsize + j9];
			}
			/* Vectorizable loop 1 */
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				output0[i] = static_cast<FAUSTFLOAT>(fRec0[i]);
			}
		}
	}

};

#endif

// --- heinzel factory (appended by tools/gen-faust.sh) ---
dsp* heinzel_make_ladder_vec() { return new heinzel_ladder_vec(); }
