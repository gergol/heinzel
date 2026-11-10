/* ------------------------------------------------------------
name: "biquad3"
Code generated with Faust 2.85.5 (https://faust.grame.fr)
Compilation options: -lang cpp -fpga-mem-th 4 -light -ct 1 -cn heinzel_biquad3_vec -es 1 -mcd 16 -mdd 1024 -mdy 33 -single -ftz 0 -vec -lv 0 -vs 32
------------------------------------------------------------ */

#ifndef  __heinzel_biquad3_vec_H__
#define  __heinzel_biquad3_vec_H__

#ifndef FAUSTFLOAT
#define FAUSTFLOAT float
#endif 

#include <algorithm>
#include <cmath>
#include <cstdint>

#ifndef FAUSTCLASS 
#define FAUSTCLASS heinzel_biquad3_vec
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


class heinzel_biquad3_vec : public dsp {
	
 private:
	
	float fRec2_perm[4];
	float fRec1_perm[4];
	float fRec0_perm[4];
	int fSampleRate;
	
 public:
	heinzel_biquad3_vec() {
	}
	
	heinzel_biquad3_vec(const heinzel_biquad3_vec&) = default;
	
	virtual ~heinzel_biquad3_vec() = default;
	
	heinzel_biquad3_vec& operator=(const heinzel_biquad3_vec&) = default;
	
	void metadata(Meta* m) { 
		m->declare("compile_options", "-lang cpp -fpga-mem-th 4 -light -ct 1 -cn heinzel_biquad3_vec -es 1 -mcd 16 -mdd 1024 -mdy 33 -single -ftz 0 -vec -lv 0 -vs 32");
		m->declare("filename", "biquad3.dsp");
		m->declare("filters.lib/fir:author", "Julius O. Smith III");
		m->declare("filters.lib/fir:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/fir:license", "MIT-style STK-4.3 license");
		m->declare("filters.lib/iir:author", "Julius O. Smith III");
		m->declare("filters.lib/iir:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/iir:license", "MIT-style STK-4.3 license");
		m->declare("filters.lib/lowpass0_highpass1", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/name", "Faust Filters Library");
		m->declare("filters.lib/tf2:author", "Julius O. Smith III");
		m->declare("filters.lib/tf2:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/tf2:license", "MIT-style STK-4.3 license");
		m->declare("filters.lib/version", "1.7.1");
		m->declare("maths.lib/author", "GRAME");
		m->declare("maths.lib/copyright", "GRAME");
		m->declare("maths.lib/license", "LGPL with exception");
		m->declare("maths.lib/name", "Faust Math Library");
		m->declare("maths.lib/version", "2.9.0");
		m->declare("name", "biquad3");
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
		for (int l0 = 0; l0 < 4; l0 = l0 + 1) {
			fRec2_perm[l0] = 0.0f;
		}
		for (int l1 = 0; l1 < 4; l1 = l1 + 1) {
			fRec1_perm[l1] = 0.0f;
		}
		for (int l2 = 0; l2 < 4; l2 = l2 + 1) {
			fRec0_perm[l2] = 0.0f;
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
	
	virtual heinzel_biquad3_vec* clone() {
		return new heinzel_biquad3_vec(*this);
	}
	
	virtual int getSampleRate() {
		return fSampleRate;
	}
	
	virtual void buildUserInterface(UI* ui_interface) {
		ui_interface->openVerticalBox("biquad3");
		ui_interface->closeBox();
	}
	
	virtual void compute(int count, FAUSTFLOAT** RESTRICT inputs, FAUSTFLOAT** RESTRICT outputs) {
		FAUSTFLOAT* input0_ptr = inputs[0];
		FAUSTFLOAT* output0_ptr = outputs[0];
		float fRec2_tmp[36];
		float* fRec2 = &fRec2_tmp[4];
		float fZec0[32];
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
				fRec2_tmp[j0] = fRec2_perm[j0];
			}
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				fRec2[i] = static_cast<float>(input0[i]) + 1.9851885f * fRec2[i - 1] - 0.9852973f * fRec2[i - 2];
			}
			/* Post code */
			for (int j1 = 0; j1 < 4; j1 = j1 + 1) {
				fRec2_perm[j1] = fRec2_tmp[vsize + j1];
			}
			/* Recursive loop 1 */
			/* Pre code */
			for (int j2 = 0; j2 < 4; j2 = j2 + 1) {
				fRec1_tmp[j2] = fRec1_perm[j2];
			}
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				fZec0[i] = 1.8953207f * fRec1[i - 1];
				fRec1[i] = 0.9926215f * (fRec2[i] + fRec2[i - 2]) + fZec0[i] - (1.985243f * fRec2[i - 1] + 0.91167533f * fRec1[i - 2]);
			}
			/* Post code */
			for (int j3 = 0; j3 < 4; j3 = j3 + 1) {
				fRec1_perm[j3] = fRec1_tmp[vsize + j3];
			}
			/* Recursive loop 2 */
			/* Pre code */
			for (int j4 = 0; j4 < 4; j4 = j4 + 1) {
				fRec0_tmp[j4] = fRec0_perm[j4];
			}
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				fRec0[i] = 1.0439531f * fRec1[i] + 0.8677223f * fRec1[i - 2] + 0.6201685f * fRec0[i - 1] - (fZec0[i] + 0.24033703f * fRec0[i - 2]);
			}
			/* Post code */
			for (int j5 = 0; j5 < 4; j5 = j5 + 1) {
				fRec0_perm[j5] = fRec0_tmp[vsize + j5];
			}
			/* Vectorizable loop 3 */
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				output0[i] = static_cast<FAUSTFLOAT>(0.31008425f * fRec0[i - 1] + 0.15504213f * (fRec0[i] + fRec0[i - 2]));
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
				fRec2_tmp[j0] = fRec2_perm[j0];
			}
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				fRec2[i] = static_cast<float>(input0[i]) + 1.9851885f * fRec2[i - 1] - 0.9852973f * fRec2[i - 2];
			}
			/* Post code */
			for (int j1 = 0; j1 < 4; j1 = j1 + 1) {
				fRec2_perm[j1] = fRec2_tmp[vsize + j1];
			}
			/* Recursive loop 1 */
			/* Pre code */
			for (int j2 = 0; j2 < 4; j2 = j2 + 1) {
				fRec1_tmp[j2] = fRec1_perm[j2];
			}
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				fZec0[i] = 1.8953207f * fRec1[i - 1];
				fRec1[i] = 0.9926215f * (fRec2[i] + fRec2[i - 2]) + fZec0[i] - (1.985243f * fRec2[i - 1] + 0.91167533f * fRec1[i - 2]);
			}
			/* Post code */
			for (int j3 = 0; j3 < 4; j3 = j3 + 1) {
				fRec1_perm[j3] = fRec1_tmp[vsize + j3];
			}
			/* Recursive loop 2 */
			/* Pre code */
			for (int j4 = 0; j4 < 4; j4 = j4 + 1) {
				fRec0_tmp[j4] = fRec0_perm[j4];
			}
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				fRec0[i] = 1.0439531f * fRec1[i] + 0.8677223f * fRec1[i - 2] + 0.6201685f * fRec0[i - 1] - (fZec0[i] + 0.24033703f * fRec0[i - 2]);
			}
			/* Post code */
			for (int j5 = 0; j5 < 4; j5 = j5 + 1) {
				fRec0_perm[j5] = fRec0_tmp[vsize + j5];
			}
			/* Vectorizable loop 3 */
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				output0[i] = static_cast<FAUSTFLOAT>(0.31008425f * fRec0[i - 1] + 0.15504213f * (fRec0[i] + fRec0[i - 2]));
			}
		}
	}

};

#endif

// --- heinzel factory (appended by tools/gen-faust.sh) ---
dsp* heinzel_make_biquad3_vec() { return new heinzel_biquad3_vec(); }
