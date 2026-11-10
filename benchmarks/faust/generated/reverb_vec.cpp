/* ------------------------------------------------------------
name: "reverb"
Code generated with Faust 2.85.5 (https://faust.grame.fr)
Compilation options: -lang cpp -fpga-mem-th 4 -light -ct 1 -cn heinzel_reverb_vec -es 1 -mcd 16 -mdd 1024 -mdy 33 -single -ftz 0 -vec -lv 0 -vs 32
------------------------------------------------------------ */

#ifndef  __heinzel_reverb_vec_H__
#define  __heinzel_reverb_vec_H__

#ifndef FAUSTFLOAT
#define FAUSTFLOAT float
#endif 

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <math.h>

#ifndef FAUSTCLASS 
#define FAUSTCLASS heinzel_reverb_vec
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


class heinzel_reverb_vec : public dsp {
	
 private:
	
	float fRec9_perm[4];
	float fYec0[8192];
	int fYec0_idx;
	int fYec0_idx_save;
	int fSampleRate;
	float fConst0;
	int iConst1;
	float fRec8_perm[4];
	float fRec11_perm[4];
	float fYec1[8192];
	int fYec1_idx;
	int fYec1_idx_save;
	int iConst2;
	float fRec10_perm[4];
	float fRec13_perm[4];
	float fYec2[8192];
	int fYec2_idx;
	int fYec2_idx_save;
	int iConst3;
	float fRec12_perm[4];
	float fRec15_perm[4];
	float fYec3[8192];
	int fYec3_idx;
	int fYec3_idx_save;
	int iConst4;
	float fRec14_perm[4];
	float fRec17_perm[4];
	float fYec4[8192];
	int fYec4_idx;
	int fYec4_idx_save;
	int iConst5;
	float fRec16_perm[4];
	float fRec19_perm[4];
	float fYec5[8192];
	int fYec5_idx;
	int fYec5_idx_save;
	int iConst6;
	float fRec18_perm[4];
	float fRec21_perm[4];
	float fYec6[8192];
	int fYec6_idx;
	int fYec6_idx_save;
	int iConst7;
	float fRec20_perm[4];
	float fRec23_perm[4];
	float fYec7[8192];
	int fYec7_idx;
	int fYec7_idx_save;
	int iConst8;
	float fRec22_perm[4];
	float fYec8[2048];
	int fYec8_idx;
	int fYec8_idx_save;
	int iConst9;
	float fRec6_perm[4];
	float fYec9[2048];
	int fYec9_idx;
	int fYec9_idx_save;
	int iConst10;
	float fRec4_perm[4];
	float fYec10[2048];
	int fYec10_idx;
	int fYec10_idx_save;
	int iConst11;
	float fRec2_perm[4];
	float fYec11[1024];
	int fYec11_idx;
	int fYec11_idx_save;
	int iConst12;
	float fRec0_perm[4];
	
 public:
	heinzel_reverb_vec() {
	}
	
	heinzel_reverb_vec(const heinzel_reverb_vec&) = default;
	
	virtual ~heinzel_reverb_vec() = default;
	
	heinzel_reverb_vec& operator=(const heinzel_reverb_vec&) = default;
	
	void metadata(Meta* m) { 
		m->declare("compile_options", "-lang cpp -fpga-mem-th 4 -light -ct 1 -cn heinzel_reverb_vec -es 1 -mcd 16 -mdd 1024 -mdy 33 -single -ftz 0 -vec -lv 0 -vs 32");
		m->declare("delays.lib/name", "Faust Delay Library");
		m->declare("delays.lib/version", "1.2.0");
		m->declare("filename", "reverb.dsp");
		m->declare("filters.lib/allpass_comb:author", "Julius O. Smith III");
		m->declare("filters.lib/allpass_comb:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/allpass_comb:license", "MIT-style STK-4.3 license");
		m->declare("filters.lib/lowpass0_highpass1", "MIT-style STK-4.3 license");
		m->declare("filters.lib/name", "Faust Filters Library");
		m->declare("filters.lib/version", "1.7.1");
		m->declare("maths.lib/author", "GRAME");
		m->declare("maths.lib/copyright", "GRAME");
		m->declare("maths.lib/license", "LGPL with exception");
		m->declare("maths.lib/name", "Faust Math Library");
		m->declare("maths.lib/version", "2.9.0");
		m->declare("name", "reverb");
		m->declare("platform.lib/name", "Generic Platform Library");
		m->declare("platform.lib/version", "1.3.0");
		m->declare("reverbs.lib/mono_freeverb:author", "Romain Michon");
		m->declare("reverbs.lib/name", "Faust Reverb Library");
		m->declare("reverbs.lib/version", "1.5.1");
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
		fConst0 = std::min<float>(1.92e+05f, std::max<float>(1.0f, static_cast<float>(fSampleRate)));
		iConst1 = std::max<int>(0, static_cast<int>(0.036666665f * fConst0) + -1);
		iConst2 = std::max<int>(0, static_cast<int>(0.035306122f * fConst0) + -1);
		iConst3 = std::max<int>(0, static_cast<int>(0.033809524f * fConst0) + -1);
		iConst4 = std::max<int>(0, static_cast<int>(0.0322449f * fConst0) + -1);
		iConst5 = std::max<int>(0, static_cast<int>(0.030748298f * fConst0) + -1);
		iConst6 = std::max<int>(0, static_cast<int>(0.028956916f * fConst0) + -1);
		iConst7 = std::max<int>(0, static_cast<int>(0.026938776f * fConst0) + -1);
		iConst8 = std::max<int>(0, static_cast<int>(0.025306122f * fConst0) + -1);
		iConst9 = std::min<int>(1024, std::max<int>(0, static_cast<int>(0.0126077095f * fConst0) + -1));
		iConst10 = std::min<int>(1024, std::max<int>(0, static_cast<int>(0.01f * fConst0) + -1));
		iConst11 = std::min<int>(1024, std::max<int>(0, static_cast<int>(0.0077324263f * fConst0) + -1));
		iConst12 = std::min<int>(1024, std::max<int>(0, static_cast<int>(0.0051020407f * fConst0) + -1));
	}
	
	virtual void instanceResetUserInterface() {
	}
	
	virtual void instanceClear() {
		for (int l0 = 0; l0 < 4; l0 = l0 + 1) {
			fRec9_perm[l0] = 0.0f;
		}
		for (int l1 = 0; l1 < 8192; l1 = l1 + 1) {
			fYec0[l1] = 0.0f;
		}
		fYec0_idx = 0;
		fYec0_idx_save = 0;
		for (int l2 = 0; l2 < 4; l2 = l2 + 1) {
			fRec8_perm[l2] = 0.0f;
		}
		for (int l3 = 0; l3 < 4; l3 = l3 + 1) {
			fRec11_perm[l3] = 0.0f;
		}
		for (int l4 = 0; l4 < 8192; l4 = l4 + 1) {
			fYec1[l4] = 0.0f;
		}
		fYec1_idx = 0;
		fYec1_idx_save = 0;
		for (int l5 = 0; l5 < 4; l5 = l5 + 1) {
			fRec10_perm[l5] = 0.0f;
		}
		for (int l6 = 0; l6 < 4; l6 = l6 + 1) {
			fRec13_perm[l6] = 0.0f;
		}
		for (int l7 = 0; l7 < 8192; l7 = l7 + 1) {
			fYec2[l7] = 0.0f;
		}
		fYec2_idx = 0;
		fYec2_idx_save = 0;
		for (int l8 = 0; l8 < 4; l8 = l8 + 1) {
			fRec12_perm[l8] = 0.0f;
		}
		for (int l9 = 0; l9 < 4; l9 = l9 + 1) {
			fRec15_perm[l9] = 0.0f;
		}
		for (int l10 = 0; l10 < 8192; l10 = l10 + 1) {
			fYec3[l10] = 0.0f;
		}
		fYec3_idx = 0;
		fYec3_idx_save = 0;
		for (int l11 = 0; l11 < 4; l11 = l11 + 1) {
			fRec14_perm[l11] = 0.0f;
		}
		for (int l12 = 0; l12 < 4; l12 = l12 + 1) {
			fRec17_perm[l12] = 0.0f;
		}
		for (int l13 = 0; l13 < 8192; l13 = l13 + 1) {
			fYec4[l13] = 0.0f;
		}
		fYec4_idx = 0;
		fYec4_idx_save = 0;
		for (int l14 = 0; l14 < 4; l14 = l14 + 1) {
			fRec16_perm[l14] = 0.0f;
		}
		for (int l15 = 0; l15 < 4; l15 = l15 + 1) {
			fRec19_perm[l15] = 0.0f;
		}
		for (int l16 = 0; l16 < 8192; l16 = l16 + 1) {
			fYec5[l16] = 0.0f;
		}
		fYec5_idx = 0;
		fYec5_idx_save = 0;
		for (int l17 = 0; l17 < 4; l17 = l17 + 1) {
			fRec18_perm[l17] = 0.0f;
		}
		for (int l18 = 0; l18 < 4; l18 = l18 + 1) {
			fRec21_perm[l18] = 0.0f;
		}
		for (int l19 = 0; l19 < 8192; l19 = l19 + 1) {
			fYec6[l19] = 0.0f;
		}
		fYec6_idx = 0;
		fYec6_idx_save = 0;
		for (int l20 = 0; l20 < 4; l20 = l20 + 1) {
			fRec20_perm[l20] = 0.0f;
		}
		for (int l21 = 0; l21 < 4; l21 = l21 + 1) {
			fRec23_perm[l21] = 0.0f;
		}
		for (int l22 = 0; l22 < 8192; l22 = l22 + 1) {
			fYec7[l22] = 0.0f;
		}
		fYec7_idx = 0;
		fYec7_idx_save = 0;
		for (int l23 = 0; l23 < 4; l23 = l23 + 1) {
			fRec22_perm[l23] = 0.0f;
		}
		for (int l24 = 0; l24 < 2048; l24 = l24 + 1) {
			fYec8[l24] = 0.0f;
		}
		fYec8_idx = 0;
		fYec8_idx_save = 0;
		for (int l25 = 0; l25 < 4; l25 = l25 + 1) {
			fRec6_perm[l25] = 0.0f;
		}
		for (int l26 = 0; l26 < 2048; l26 = l26 + 1) {
			fYec9[l26] = 0.0f;
		}
		fYec9_idx = 0;
		fYec9_idx_save = 0;
		for (int l27 = 0; l27 < 4; l27 = l27 + 1) {
			fRec4_perm[l27] = 0.0f;
		}
		for (int l28 = 0; l28 < 2048; l28 = l28 + 1) {
			fYec10[l28] = 0.0f;
		}
		fYec10_idx = 0;
		fYec10_idx_save = 0;
		for (int l29 = 0; l29 < 4; l29 = l29 + 1) {
			fRec2_perm[l29] = 0.0f;
		}
		for (int l30 = 0; l30 < 1024; l30 = l30 + 1) {
			fYec11[l30] = 0.0f;
		}
		fYec11_idx = 0;
		fYec11_idx_save = 0;
		for (int l31 = 0; l31 < 4; l31 = l31 + 1) {
			fRec0_perm[l31] = 0.0f;
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
	
	virtual heinzel_reverb_vec* clone() {
		return new heinzel_reverb_vec(*this);
	}
	
	virtual int getSampleRate() {
		return fSampleRate;
	}
	
	virtual void buildUserInterface(UI* ui_interface) {
		ui_interface->openVerticalBox("reverb");
		ui_interface->closeBox();
	}
	
	virtual void compute(int count, FAUSTFLOAT** RESTRICT inputs, FAUSTFLOAT** RESTRICT outputs) {
		FAUSTFLOAT* input0_ptr = inputs[0];
		FAUSTFLOAT* output0_ptr = outputs[0];
		float fRec9_tmp[36];
		float* fRec9 = &fRec9_tmp[4];
		float fRec8_tmp[36];
		float* fRec8 = &fRec8_tmp[4];
		float fRec11_tmp[36];
		float* fRec11 = &fRec11_tmp[4];
		float fRec10_tmp[36];
		float* fRec10 = &fRec10_tmp[4];
		float fRec13_tmp[36];
		float* fRec13 = &fRec13_tmp[4];
		float fRec12_tmp[36];
		float* fRec12 = &fRec12_tmp[4];
		float fRec15_tmp[36];
		float* fRec15 = &fRec15_tmp[4];
		float fRec14_tmp[36];
		float* fRec14 = &fRec14_tmp[4];
		float fRec17_tmp[36];
		float* fRec17 = &fRec17_tmp[4];
		float fRec16_tmp[36];
		float* fRec16 = &fRec16_tmp[4];
		float fRec19_tmp[36];
		float* fRec19 = &fRec19_tmp[4];
		float fRec18_tmp[36];
		float* fRec18 = &fRec18_tmp[4];
		float fRec21_tmp[36];
		float* fRec21 = &fRec21_tmp[4];
		float fRec20_tmp[36];
		float* fRec20 = &fRec20_tmp[4];
		float fRec23_tmp[36];
		float* fRec23 = &fRec23_tmp[4];
		float fRec22_tmp[36];
		float* fRec22 = &fRec22_tmp[4];
		float fRec6_tmp[36];
		float* fRec6 = &fRec6_tmp[4];
		float fRec7[32];
		float fRec4_tmp[36];
		float* fRec4 = &fRec4_tmp[4];
		float fRec5[32];
		float fRec2_tmp[36];
		float* fRec2 = &fRec2_tmp[4];
		float fRec3[32];
		float fRec0_tmp[36];
		float* fRec0 = &fRec0_tmp[4];
		float fRec1[32];
		int vindex = 0;
		/* Main loop */
		for (vindex = 0; vindex <= (count - 32); vindex = vindex + 32) {
			FAUSTFLOAT* input0 = &input0_ptr[vindex];
			FAUSTFLOAT* output0 = &output0_ptr[vindex];
			int vsize = 32;
			/* Recursive loop 0 */
			/* Pre code */
			for (int j0 = 0; j0 < 4; j0 = j0 + 1) {
				fRec9_tmp[j0] = fRec9_perm[j0];
			}
			fYec0_idx = (fYec0_idx + fYec0_idx_save) & 8191;
			for (int j2 = 0; j2 < 4; j2 = j2 + 1) {
				fRec8_tmp[j2] = fRec8_perm[j2];
			}
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				fRec9[i] = 0.2f * fRec9[i - 1] + 0.8f * fRec8[i - 1];
				fYec0[(i + fYec0_idx) & 8191] = static_cast<float>(input0[i]) + 0.84f * fRec9[i];
				fRec8[i] = fYec0[(i + fYec0_idx - iConst1) & 8191];
			}
			/* Post code */
			fYec0_idx_save = vsize;
			for (int j1 = 0; j1 < 4; j1 = j1 + 1) {
				fRec9_perm[j1] = fRec9_tmp[vsize + j1];
			}
			for (int j3 = 0; j3 < 4; j3 = j3 + 1) {
				fRec8_perm[j3] = fRec8_tmp[vsize + j3];
			}
			/* Recursive loop 1 */
			/* Pre code */
			for (int j4 = 0; j4 < 4; j4 = j4 + 1) {
				fRec11_tmp[j4] = fRec11_perm[j4];
			}
			fYec1_idx = (fYec1_idx + fYec1_idx_save) & 8191;
			for (int j6 = 0; j6 < 4; j6 = j6 + 1) {
				fRec10_tmp[j6] = fRec10_perm[j6];
			}
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				fRec11[i] = 0.2f * fRec11[i - 1] + 0.8f * fRec10[i - 1];
				fYec1[(i + fYec1_idx) & 8191] = static_cast<float>(input0[i]) + 0.84f * fRec11[i];
				fRec10[i] = fYec1[(i + fYec1_idx - iConst2) & 8191];
			}
			/* Post code */
			fYec1_idx_save = vsize;
			for (int j5 = 0; j5 < 4; j5 = j5 + 1) {
				fRec11_perm[j5] = fRec11_tmp[vsize + j5];
			}
			for (int j7 = 0; j7 < 4; j7 = j7 + 1) {
				fRec10_perm[j7] = fRec10_tmp[vsize + j7];
			}
			/* Recursive loop 2 */
			/* Pre code */
			for (int j8 = 0; j8 < 4; j8 = j8 + 1) {
				fRec13_tmp[j8] = fRec13_perm[j8];
			}
			fYec2_idx = (fYec2_idx + fYec2_idx_save) & 8191;
			for (int j10 = 0; j10 < 4; j10 = j10 + 1) {
				fRec12_tmp[j10] = fRec12_perm[j10];
			}
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				fRec13[i] = 0.2f * fRec13[i - 1] + 0.8f * fRec12[i - 1];
				fYec2[(i + fYec2_idx) & 8191] = static_cast<float>(input0[i]) + 0.84f * fRec13[i];
				fRec12[i] = fYec2[(i + fYec2_idx - iConst3) & 8191];
			}
			/* Post code */
			fYec2_idx_save = vsize;
			for (int j9 = 0; j9 < 4; j9 = j9 + 1) {
				fRec13_perm[j9] = fRec13_tmp[vsize + j9];
			}
			for (int j11 = 0; j11 < 4; j11 = j11 + 1) {
				fRec12_perm[j11] = fRec12_tmp[vsize + j11];
			}
			/* Recursive loop 3 */
			/* Pre code */
			for (int j12 = 0; j12 < 4; j12 = j12 + 1) {
				fRec15_tmp[j12] = fRec15_perm[j12];
			}
			fYec3_idx = (fYec3_idx + fYec3_idx_save) & 8191;
			for (int j14 = 0; j14 < 4; j14 = j14 + 1) {
				fRec14_tmp[j14] = fRec14_perm[j14];
			}
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				fRec15[i] = 0.2f * fRec15[i - 1] + 0.8f * fRec14[i - 1];
				fYec3[(i + fYec3_idx) & 8191] = static_cast<float>(input0[i]) + 0.84f * fRec15[i];
				fRec14[i] = fYec3[(i + fYec3_idx - iConst4) & 8191];
			}
			/* Post code */
			fYec3_idx_save = vsize;
			for (int j13 = 0; j13 < 4; j13 = j13 + 1) {
				fRec15_perm[j13] = fRec15_tmp[vsize + j13];
			}
			for (int j15 = 0; j15 < 4; j15 = j15 + 1) {
				fRec14_perm[j15] = fRec14_tmp[vsize + j15];
			}
			/* Recursive loop 4 */
			/* Pre code */
			for (int j16 = 0; j16 < 4; j16 = j16 + 1) {
				fRec17_tmp[j16] = fRec17_perm[j16];
			}
			fYec4_idx = (fYec4_idx + fYec4_idx_save) & 8191;
			for (int j18 = 0; j18 < 4; j18 = j18 + 1) {
				fRec16_tmp[j18] = fRec16_perm[j18];
			}
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				fRec17[i] = 0.2f * fRec17[i - 1] + 0.8f * fRec16[i - 1];
				fYec4[(i + fYec4_idx) & 8191] = static_cast<float>(input0[i]) + 0.84f * fRec17[i];
				fRec16[i] = fYec4[(i + fYec4_idx - iConst5) & 8191];
			}
			/* Post code */
			fYec4_idx_save = vsize;
			for (int j17 = 0; j17 < 4; j17 = j17 + 1) {
				fRec17_perm[j17] = fRec17_tmp[vsize + j17];
			}
			for (int j19 = 0; j19 < 4; j19 = j19 + 1) {
				fRec16_perm[j19] = fRec16_tmp[vsize + j19];
			}
			/* Recursive loop 5 */
			/* Pre code */
			for (int j20 = 0; j20 < 4; j20 = j20 + 1) {
				fRec19_tmp[j20] = fRec19_perm[j20];
			}
			fYec5_idx = (fYec5_idx + fYec5_idx_save) & 8191;
			for (int j22 = 0; j22 < 4; j22 = j22 + 1) {
				fRec18_tmp[j22] = fRec18_perm[j22];
			}
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				fRec19[i] = 0.2f * fRec19[i - 1] + 0.8f * fRec18[i - 1];
				fYec5[(i + fYec5_idx) & 8191] = static_cast<float>(input0[i]) + 0.84f * fRec19[i];
				fRec18[i] = fYec5[(i + fYec5_idx - iConst6) & 8191];
			}
			/* Post code */
			fYec5_idx_save = vsize;
			for (int j21 = 0; j21 < 4; j21 = j21 + 1) {
				fRec19_perm[j21] = fRec19_tmp[vsize + j21];
			}
			for (int j23 = 0; j23 < 4; j23 = j23 + 1) {
				fRec18_perm[j23] = fRec18_tmp[vsize + j23];
			}
			/* Recursive loop 6 */
			/* Pre code */
			for (int j24 = 0; j24 < 4; j24 = j24 + 1) {
				fRec21_tmp[j24] = fRec21_perm[j24];
			}
			fYec6_idx = (fYec6_idx + fYec6_idx_save) & 8191;
			for (int j26 = 0; j26 < 4; j26 = j26 + 1) {
				fRec20_tmp[j26] = fRec20_perm[j26];
			}
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				fRec21[i] = 0.2f * fRec21[i - 1] + 0.8f * fRec20[i - 1];
				fYec6[(i + fYec6_idx) & 8191] = static_cast<float>(input0[i]) + 0.84f * fRec21[i];
				fRec20[i] = fYec6[(i + fYec6_idx - iConst7) & 8191];
			}
			/* Post code */
			fYec6_idx_save = vsize;
			for (int j25 = 0; j25 < 4; j25 = j25 + 1) {
				fRec21_perm[j25] = fRec21_tmp[vsize + j25];
			}
			for (int j27 = 0; j27 < 4; j27 = j27 + 1) {
				fRec20_perm[j27] = fRec20_tmp[vsize + j27];
			}
			/* Recursive loop 7 */
			/* Pre code */
			for (int j28 = 0; j28 < 4; j28 = j28 + 1) {
				fRec23_tmp[j28] = fRec23_perm[j28];
			}
			fYec7_idx = (fYec7_idx + fYec7_idx_save) & 8191;
			for (int j30 = 0; j30 < 4; j30 = j30 + 1) {
				fRec22_tmp[j30] = fRec22_perm[j30];
			}
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				fRec23[i] = 0.2f * fRec23[i - 1] + 0.8f * fRec22[i - 1];
				fYec7[(i + fYec7_idx) & 8191] = static_cast<float>(input0[i]) + 0.84f * fRec23[i];
				fRec22[i] = fYec7[(i + fYec7_idx - iConst8) & 8191];
			}
			/* Post code */
			fYec7_idx_save = vsize;
			for (int j29 = 0; j29 < 4; j29 = j29 + 1) {
				fRec23_perm[j29] = fRec23_tmp[vsize + j29];
			}
			for (int j31 = 0; j31 < 4; j31 = j31 + 1) {
				fRec22_perm[j31] = fRec22_tmp[vsize + j31];
			}
			/* Recursive loop 8 */
			/* Pre code */
			fYec8_idx = (fYec8_idx + fYec8_idx_save) & 2047;
			for (int j32 = 0; j32 < 4; j32 = j32 + 1) {
				fRec6_tmp[j32] = fRec6_perm[j32];
			}
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				fYec8[(i + fYec8_idx) & 2047] = fRec8[i - 1] + fRec10[i - 1] + fRec12[i - 1] + fRec14[i - 1] + fRec16[i - 1] + fRec18[i - 1] + fRec20[i - 1] + 0.5f * fRec6[i - 1] + fRec22[i - 1];
				fRec6[i] = fYec8[(i + fYec8_idx - iConst9) & 2047];
				fRec7[i] = -(0.5f * fYec8[(i + fYec8_idx) & 2047]);
			}
			/* Post code */
			fYec8_idx_save = vsize;
			for (int j33 = 0; j33 < 4; j33 = j33 + 1) {
				fRec6_perm[j33] = fRec6_tmp[vsize + j33];
			}
			/* Recursive loop 9 */
			/* Pre code */
			fYec9_idx = (fYec9_idx + fYec9_idx_save) & 2047;
			for (int j34 = 0; j34 < 4; j34 = j34 + 1) {
				fRec4_tmp[j34] = fRec4_perm[j34];
			}
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				fYec9[(i + fYec9_idx) & 2047] = fRec6[i - 1] + fRec7[i] + 0.5f * fRec4[i - 1];
				fRec4[i] = fYec9[(i + fYec9_idx - iConst10) & 2047];
				fRec5[i] = -(0.5f * fYec9[(i + fYec9_idx) & 2047]);
			}
			/* Post code */
			fYec9_idx_save = vsize;
			for (int j35 = 0; j35 < 4; j35 = j35 + 1) {
				fRec4_perm[j35] = fRec4_tmp[vsize + j35];
			}
			/* Recursive loop 10 */
			/* Pre code */
			fYec10_idx = (fYec10_idx + fYec10_idx_save) & 2047;
			for (int j36 = 0; j36 < 4; j36 = j36 + 1) {
				fRec2_tmp[j36] = fRec2_perm[j36];
			}
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				fYec10[(i + fYec10_idx) & 2047] = fRec4[i - 1] + fRec5[i] + 0.5f * fRec2[i - 1];
				fRec2[i] = fYec10[(i + fYec10_idx - iConst11) & 2047];
				fRec3[i] = -(0.5f * fYec10[(i + fYec10_idx) & 2047]);
			}
			/* Post code */
			fYec10_idx_save = vsize;
			for (int j37 = 0; j37 < 4; j37 = j37 + 1) {
				fRec2_perm[j37] = fRec2_tmp[vsize + j37];
			}
			/* Recursive loop 11 */
			/* Pre code */
			fYec11_idx = (fYec11_idx + fYec11_idx_save) & 1023;
			for (int j38 = 0; j38 < 4; j38 = j38 + 1) {
				fRec0_tmp[j38] = fRec0_perm[j38];
			}
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				fYec11[(i + fYec11_idx) & 1023] = fRec2[i - 1] + fRec3[i] + 0.5f * fRec0[i - 1];
				fRec0[i] = fYec11[(i + fYec11_idx - iConst12) & 1023];
				fRec1[i] = -(0.5f * fYec11[(i + fYec11_idx) & 1023]);
			}
			/* Post code */
			fYec11_idx_save = vsize;
			for (int j39 = 0; j39 < 4; j39 = j39 + 1) {
				fRec0_perm[j39] = fRec0_tmp[vsize + j39];
			}
			/* Vectorizable loop 12 */
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				output0[i] = static_cast<FAUSTFLOAT>(0.33f * (fRec1[i] + fRec0[i - 1]));
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
				fRec9_tmp[j0] = fRec9_perm[j0];
			}
			fYec0_idx = (fYec0_idx + fYec0_idx_save) & 8191;
			for (int j2 = 0; j2 < 4; j2 = j2 + 1) {
				fRec8_tmp[j2] = fRec8_perm[j2];
			}
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				fRec9[i] = 0.2f * fRec9[i - 1] + 0.8f * fRec8[i - 1];
				fYec0[(i + fYec0_idx) & 8191] = static_cast<float>(input0[i]) + 0.84f * fRec9[i];
				fRec8[i] = fYec0[(i + fYec0_idx - iConst1) & 8191];
			}
			/* Post code */
			fYec0_idx_save = vsize;
			for (int j1 = 0; j1 < 4; j1 = j1 + 1) {
				fRec9_perm[j1] = fRec9_tmp[vsize + j1];
			}
			for (int j3 = 0; j3 < 4; j3 = j3 + 1) {
				fRec8_perm[j3] = fRec8_tmp[vsize + j3];
			}
			/* Recursive loop 1 */
			/* Pre code */
			for (int j4 = 0; j4 < 4; j4 = j4 + 1) {
				fRec11_tmp[j4] = fRec11_perm[j4];
			}
			fYec1_idx = (fYec1_idx + fYec1_idx_save) & 8191;
			for (int j6 = 0; j6 < 4; j6 = j6 + 1) {
				fRec10_tmp[j6] = fRec10_perm[j6];
			}
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				fRec11[i] = 0.2f * fRec11[i - 1] + 0.8f * fRec10[i - 1];
				fYec1[(i + fYec1_idx) & 8191] = static_cast<float>(input0[i]) + 0.84f * fRec11[i];
				fRec10[i] = fYec1[(i + fYec1_idx - iConst2) & 8191];
			}
			/* Post code */
			fYec1_idx_save = vsize;
			for (int j5 = 0; j5 < 4; j5 = j5 + 1) {
				fRec11_perm[j5] = fRec11_tmp[vsize + j5];
			}
			for (int j7 = 0; j7 < 4; j7 = j7 + 1) {
				fRec10_perm[j7] = fRec10_tmp[vsize + j7];
			}
			/* Recursive loop 2 */
			/* Pre code */
			for (int j8 = 0; j8 < 4; j8 = j8 + 1) {
				fRec13_tmp[j8] = fRec13_perm[j8];
			}
			fYec2_idx = (fYec2_idx + fYec2_idx_save) & 8191;
			for (int j10 = 0; j10 < 4; j10 = j10 + 1) {
				fRec12_tmp[j10] = fRec12_perm[j10];
			}
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				fRec13[i] = 0.2f * fRec13[i - 1] + 0.8f * fRec12[i - 1];
				fYec2[(i + fYec2_idx) & 8191] = static_cast<float>(input0[i]) + 0.84f * fRec13[i];
				fRec12[i] = fYec2[(i + fYec2_idx - iConst3) & 8191];
			}
			/* Post code */
			fYec2_idx_save = vsize;
			for (int j9 = 0; j9 < 4; j9 = j9 + 1) {
				fRec13_perm[j9] = fRec13_tmp[vsize + j9];
			}
			for (int j11 = 0; j11 < 4; j11 = j11 + 1) {
				fRec12_perm[j11] = fRec12_tmp[vsize + j11];
			}
			/* Recursive loop 3 */
			/* Pre code */
			for (int j12 = 0; j12 < 4; j12 = j12 + 1) {
				fRec15_tmp[j12] = fRec15_perm[j12];
			}
			fYec3_idx = (fYec3_idx + fYec3_idx_save) & 8191;
			for (int j14 = 0; j14 < 4; j14 = j14 + 1) {
				fRec14_tmp[j14] = fRec14_perm[j14];
			}
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				fRec15[i] = 0.2f * fRec15[i - 1] + 0.8f * fRec14[i - 1];
				fYec3[(i + fYec3_idx) & 8191] = static_cast<float>(input0[i]) + 0.84f * fRec15[i];
				fRec14[i] = fYec3[(i + fYec3_idx - iConst4) & 8191];
			}
			/* Post code */
			fYec3_idx_save = vsize;
			for (int j13 = 0; j13 < 4; j13 = j13 + 1) {
				fRec15_perm[j13] = fRec15_tmp[vsize + j13];
			}
			for (int j15 = 0; j15 < 4; j15 = j15 + 1) {
				fRec14_perm[j15] = fRec14_tmp[vsize + j15];
			}
			/* Recursive loop 4 */
			/* Pre code */
			for (int j16 = 0; j16 < 4; j16 = j16 + 1) {
				fRec17_tmp[j16] = fRec17_perm[j16];
			}
			fYec4_idx = (fYec4_idx + fYec4_idx_save) & 8191;
			for (int j18 = 0; j18 < 4; j18 = j18 + 1) {
				fRec16_tmp[j18] = fRec16_perm[j18];
			}
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				fRec17[i] = 0.2f * fRec17[i - 1] + 0.8f * fRec16[i - 1];
				fYec4[(i + fYec4_idx) & 8191] = static_cast<float>(input0[i]) + 0.84f * fRec17[i];
				fRec16[i] = fYec4[(i + fYec4_idx - iConst5) & 8191];
			}
			/* Post code */
			fYec4_idx_save = vsize;
			for (int j17 = 0; j17 < 4; j17 = j17 + 1) {
				fRec17_perm[j17] = fRec17_tmp[vsize + j17];
			}
			for (int j19 = 0; j19 < 4; j19 = j19 + 1) {
				fRec16_perm[j19] = fRec16_tmp[vsize + j19];
			}
			/* Recursive loop 5 */
			/* Pre code */
			for (int j20 = 0; j20 < 4; j20 = j20 + 1) {
				fRec19_tmp[j20] = fRec19_perm[j20];
			}
			fYec5_idx = (fYec5_idx + fYec5_idx_save) & 8191;
			for (int j22 = 0; j22 < 4; j22 = j22 + 1) {
				fRec18_tmp[j22] = fRec18_perm[j22];
			}
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				fRec19[i] = 0.2f * fRec19[i - 1] + 0.8f * fRec18[i - 1];
				fYec5[(i + fYec5_idx) & 8191] = static_cast<float>(input0[i]) + 0.84f * fRec19[i];
				fRec18[i] = fYec5[(i + fYec5_idx - iConst6) & 8191];
			}
			/* Post code */
			fYec5_idx_save = vsize;
			for (int j21 = 0; j21 < 4; j21 = j21 + 1) {
				fRec19_perm[j21] = fRec19_tmp[vsize + j21];
			}
			for (int j23 = 0; j23 < 4; j23 = j23 + 1) {
				fRec18_perm[j23] = fRec18_tmp[vsize + j23];
			}
			/* Recursive loop 6 */
			/* Pre code */
			for (int j24 = 0; j24 < 4; j24 = j24 + 1) {
				fRec21_tmp[j24] = fRec21_perm[j24];
			}
			fYec6_idx = (fYec6_idx + fYec6_idx_save) & 8191;
			for (int j26 = 0; j26 < 4; j26 = j26 + 1) {
				fRec20_tmp[j26] = fRec20_perm[j26];
			}
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				fRec21[i] = 0.2f * fRec21[i - 1] + 0.8f * fRec20[i - 1];
				fYec6[(i + fYec6_idx) & 8191] = static_cast<float>(input0[i]) + 0.84f * fRec21[i];
				fRec20[i] = fYec6[(i + fYec6_idx - iConst7) & 8191];
			}
			/* Post code */
			fYec6_idx_save = vsize;
			for (int j25 = 0; j25 < 4; j25 = j25 + 1) {
				fRec21_perm[j25] = fRec21_tmp[vsize + j25];
			}
			for (int j27 = 0; j27 < 4; j27 = j27 + 1) {
				fRec20_perm[j27] = fRec20_tmp[vsize + j27];
			}
			/* Recursive loop 7 */
			/* Pre code */
			for (int j28 = 0; j28 < 4; j28 = j28 + 1) {
				fRec23_tmp[j28] = fRec23_perm[j28];
			}
			fYec7_idx = (fYec7_idx + fYec7_idx_save) & 8191;
			for (int j30 = 0; j30 < 4; j30 = j30 + 1) {
				fRec22_tmp[j30] = fRec22_perm[j30];
			}
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				fRec23[i] = 0.2f * fRec23[i - 1] + 0.8f * fRec22[i - 1];
				fYec7[(i + fYec7_idx) & 8191] = static_cast<float>(input0[i]) + 0.84f * fRec23[i];
				fRec22[i] = fYec7[(i + fYec7_idx - iConst8) & 8191];
			}
			/* Post code */
			fYec7_idx_save = vsize;
			for (int j29 = 0; j29 < 4; j29 = j29 + 1) {
				fRec23_perm[j29] = fRec23_tmp[vsize + j29];
			}
			for (int j31 = 0; j31 < 4; j31 = j31 + 1) {
				fRec22_perm[j31] = fRec22_tmp[vsize + j31];
			}
			/* Recursive loop 8 */
			/* Pre code */
			fYec8_idx = (fYec8_idx + fYec8_idx_save) & 2047;
			for (int j32 = 0; j32 < 4; j32 = j32 + 1) {
				fRec6_tmp[j32] = fRec6_perm[j32];
			}
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				fYec8[(i + fYec8_idx) & 2047] = fRec8[i - 1] + fRec10[i - 1] + fRec12[i - 1] + fRec14[i - 1] + fRec16[i - 1] + fRec18[i - 1] + fRec20[i - 1] + 0.5f * fRec6[i - 1] + fRec22[i - 1];
				fRec6[i] = fYec8[(i + fYec8_idx - iConst9) & 2047];
				fRec7[i] = -(0.5f * fYec8[(i + fYec8_idx) & 2047]);
			}
			/* Post code */
			fYec8_idx_save = vsize;
			for (int j33 = 0; j33 < 4; j33 = j33 + 1) {
				fRec6_perm[j33] = fRec6_tmp[vsize + j33];
			}
			/* Recursive loop 9 */
			/* Pre code */
			fYec9_idx = (fYec9_idx + fYec9_idx_save) & 2047;
			for (int j34 = 0; j34 < 4; j34 = j34 + 1) {
				fRec4_tmp[j34] = fRec4_perm[j34];
			}
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				fYec9[(i + fYec9_idx) & 2047] = fRec6[i - 1] + fRec7[i] + 0.5f * fRec4[i - 1];
				fRec4[i] = fYec9[(i + fYec9_idx - iConst10) & 2047];
				fRec5[i] = -(0.5f * fYec9[(i + fYec9_idx) & 2047]);
			}
			/* Post code */
			fYec9_idx_save = vsize;
			for (int j35 = 0; j35 < 4; j35 = j35 + 1) {
				fRec4_perm[j35] = fRec4_tmp[vsize + j35];
			}
			/* Recursive loop 10 */
			/* Pre code */
			fYec10_idx = (fYec10_idx + fYec10_idx_save) & 2047;
			for (int j36 = 0; j36 < 4; j36 = j36 + 1) {
				fRec2_tmp[j36] = fRec2_perm[j36];
			}
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				fYec10[(i + fYec10_idx) & 2047] = fRec4[i - 1] + fRec5[i] + 0.5f * fRec2[i - 1];
				fRec2[i] = fYec10[(i + fYec10_idx - iConst11) & 2047];
				fRec3[i] = -(0.5f * fYec10[(i + fYec10_idx) & 2047]);
			}
			/* Post code */
			fYec10_idx_save = vsize;
			for (int j37 = 0; j37 < 4; j37 = j37 + 1) {
				fRec2_perm[j37] = fRec2_tmp[vsize + j37];
			}
			/* Recursive loop 11 */
			/* Pre code */
			fYec11_idx = (fYec11_idx + fYec11_idx_save) & 1023;
			for (int j38 = 0; j38 < 4; j38 = j38 + 1) {
				fRec0_tmp[j38] = fRec0_perm[j38];
			}
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				fYec11[(i + fYec11_idx) & 1023] = fRec2[i - 1] + fRec3[i] + 0.5f * fRec0[i - 1];
				fRec0[i] = fYec11[(i + fYec11_idx - iConst12) & 1023];
				fRec1[i] = -(0.5f * fYec11[(i + fYec11_idx) & 1023]);
			}
			/* Post code */
			fYec11_idx_save = vsize;
			for (int j39 = 0; j39 < 4; j39 = j39 + 1) {
				fRec0_perm[j39] = fRec0_tmp[vsize + j39];
			}
			/* Vectorizable loop 12 */
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				output0[i] = static_cast<FAUSTFLOAT>(0.33f * (fRec1[i] + fRec0[i - 1]));
			}
		}
	}

};

#endif

// --- heinzel factory (appended by tools/gen-faust.sh) ---
dsp* heinzel_make_reverb_vec() { return new heinzel_reverb_vec(); }
