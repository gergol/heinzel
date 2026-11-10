/* ------------------------------------------------------------
name: "ladder16"
Code generated with Faust 2.85.5 (https://faust.grame.fr)
Compilation options: -lang cpp -fpga-mem-th 4 -light -ct 1 -cn heinzel_ladder16_vec -es 1 -mcd 16 -mdd 1024 -mdy 33 -single -ftz 0 -vec -lv 0 -vs 32
------------------------------------------------------------ */

#ifndef  __heinzel_ladder16_vec_H__
#define  __heinzel_ladder16_vec_H__

#ifndef FAUSTFLOAT
#define FAUSTFLOAT float
#endif 

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <math.h>

#ifndef FAUSTCLASS 
#define FAUSTCLASS heinzel_ladder16_vec
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

static float heinzel_ladder16_vec_faustpower4_f(float value) {
	return value * value * value * value;
}

class heinzel_ladder16_vec : public dsp {
	
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
	float fRec9_perm[4];
	float fRec8_perm[4];
	float fRec7_perm[4];
	float fRec6_perm[4];
	float fRec5_perm[4];
	float fRec14_perm[4];
	float fRec13_perm[4];
	float fRec12_perm[4];
	float fRec11_perm[4];
	float fRec10_perm[4];
	float fRec19_perm[4];
	float fRec18_perm[4];
	float fRec17_perm[4];
	float fRec16_perm[4];
	float fRec15_perm[4];
	float fRec24_perm[4];
	float fRec23_perm[4];
	float fRec22_perm[4];
	float fRec21_perm[4];
	float fRec20_perm[4];
	float fRec29_perm[4];
	float fRec28_perm[4];
	float fRec27_perm[4];
	float fRec26_perm[4];
	float fRec25_perm[4];
	float fRec34_perm[4];
	float fRec33_perm[4];
	float fRec32_perm[4];
	float fRec31_perm[4];
	float fRec30_perm[4];
	float fRec39_perm[4];
	float fRec38_perm[4];
	float fRec37_perm[4];
	float fRec36_perm[4];
	float fRec35_perm[4];
	float fRec44_perm[4];
	float fRec43_perm[4];
	float fRec42_perm[4];
	float fRec41_perm[4];
	float fRec40_perm[4];
	float fRec49_perm[4];
	float fRec48_perm[4];
	float fRec47_perm[4];
	float fRec46_perm[4];
	float fRec45_perm[4];
	float fRec54_perm[4];
	float fRec53_perm[4];
	float fRec52_perm[4];
	float fRec51_perm[4];
	float fRec50_perm[4];
	float fRec59_perm[4];
	float fRec58_perm[4];
	float fRec57_perm[4];
	float fRec56_perm[4];
	float fRec55_perm[4];
	float fRec64_perm[4];
	float fRec63_perm[4];
	float fRec62_perm[4];
	float fRec61_perm[4];
	float fRec60_perm[4];
	float fRec69_perm[4];
	float fRec68_perm[4];
	float fRec67_perm[4];
	float fRec66_perm[4];
	float fRec65_perm[4];
	float fRec74_perm[4];
	float fRec73_perm[4];
	float fRec72_perm[4];
	float fRec71_perm[4];
	float fRec70_perm[4];
	float fRec79_perm[4];
	float fRec78_perm[4];
	float fRec77_perm[4];
	float fRec76_perm[4];
	float fRec75_perm[4];
	
 public:
	heinzel_ladder16_vec() {
	}
	
	heinzel_ladder16_vec(const heinzel_ladder16_vec&) = default;
	
	virtual ~heinzel_ladder16_vec() = default;
	
	heinzel_ladder16_vec& operator=(const heinzel_ladder16_vec&) = default;
	
	void metadata(Meta* m) { 
		m->declare("compile_options", "-lang cpp -fpga-mem-th 4 -light -ct 1 -cn heinzel_ladder16_vec -es 1 -mcd 16 -mdd 1024 -mdy 33 -single -ftz 0 -vec -lv 0 -vs 32");
		m->declare("filename", "ladder16.dsp");
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
		m->declare("name", "ladder16");
		m->declare("platform.lib/name", "Generic Platform Library");
		m->declare("platform.lib/version", "1.3.0");
		m->declare("vaeffects.lib/moog_vcf:author", "Julius O. Smith III");
		m->declare("vaeffects.lib/moog_vcf:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("vaeffects.lib/moog_vcf:license", "MIT-style STK-4.3 license");
		m->declare("vaeffects.lib/name", "Faust Virtual Analog Filter Effect Library");
		m->declare("vaeffects.lib/version", "1.5.0");
	}

	virtual int getNumInputs() {
		return 16;
	}
	virtual int getNumOutputs() {
		return 16;
	}
	
	static void classInit(int sample_rate) {
	}
	
	virtual void instanceConstants(int sample_rate) {
		fSampleRate = sample_rate;
		fConst0 = 6283.1855f / std::min<float>(1.92e+05f, std::max<float>(1.0f, static_cast<float>(fSampleRate)));
		fConst1 = 1.0f - fConst0;
		fConst2 = heinzel_ladder16_vec_faustpower4_f(fConst0);
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
		for (int l5 = 0; l5 < 4; l5 = l5 + 1) {
			fRec9_perm[l5] = 0.0f;
		}
		for (int l6 = 0; l6 < 4; l6 = l6 + 1) {
			fRec8_perm[l6] = 0.0f;
		}
		for (int l7 = 0; l7 < 4; l7 = l7 + 1) {
			fRec7_perm[l7] = 0.0f;
		}
		for (int l8 = 0; l8 < 4; l8 = l8 + 1) {
			fRec6_perm[l8] = 0.0f;
		}
		for (int l9 = 0; l9 < 4; l9 = l9 + 1) {
			fRec5_perm[l9] = 0.0f;
		}
		for (int l10 = 0; l10 < 4; l10 = l10 + 1) {
			fRec14_perm[l10] = 0.0f;
		}
		for (int l11 = 0; l11 < 4; l11 = l11 + 1) {
			fRec13_perm[l11] = 0.0f;
		}
		for (int l12 = 0; l12 < 4; l12 = l12 + 1) {
			fRec12_perm[l12] = 0.0f;
		}
		for (int l13 = 0; l13 < 4; l13 = l13 + 1) {
			fRec11_perm[l13] = 0.0f;
		}
		for (int l14 = 0; l14 < 4; l14 = l14 + 1) {
			fRec10_perm[l14] = 0.0f;
		}
		for (int l15 = 0; l15 < 4; l15 = l15 + 1) {
			fRec19_perm[l15] = 0.0f;
		}
		for (int l16 = 0; l16 < 4; l16 = l16 + 1) {
			fRec18_perm[l16] = 0.0f;
		}
		for (int l17 = 0; l17 < 4; l17 = l17 + 1) {
			fRec17_perm[l17] = 0.0f;
		}
		for (int l18 = 0; l18 < 4; l18 = l18 + 1) {
			fRec16_perm[l18] = 0.0f;
		}
		for (int l19 = 0; l19 < 4; l19 = l19 + 1) {
			fRec15_perm[l19] = 0.0f;
		}
		for (int l20 = 0; l20 < 4; l20 = l20 + 1) {
			fRec24_perm[l20] = 0.0f;
		}
		for (int l21 = 0; l21 < 4; l21 = l21 + 1) {
			fRec23_perm[l21] = 0.0f;
		}
		for (int l22 = 0; l22 < 4; l22 = l22 + 1) {
			fRec22_perm[l22] = 0.0f;
		}
		for (int l23 = 0; l23 < 4; l23 = l23 + 1) {
			fRec21_perm[l23] = 0.0f;
		}
		for (int l24 = 0; l24 < 4; l24 = l24 + 1) {
			fRec20_perm[l24] = 0.0f;
		}
		for (int l25 = 0; l25 < 4; l25 = l25 + 1) {
			fRec29_perm[l25] = 0.0f;
		}
		for (int l26 = 0; l26 < 4; l26 = l26 + 1) {
			fRec28_perm[l26] = 0.0f;
		}
		for (int l27 = 0; l27 < 4; l27 = l27 + 1) {
			fRec27_perm[l27] = 0.0f;
		}
		for (int l28 = 0; l28 < 4; l28 = l28 + 1) {
			fRec26_perm[l28] = 0.0f;
		}
		for (int l29 = 0; l29 < 4; l29 = l29 + 1) {
			fRec25_perm[l29] = 0.0f;
		}
		for (int l30 = 0; l30 < 4; l30 = l30 + 1) {
			fRec34_perm[l30] = 0.0f;
		}
		for (int l31 = 0; l31 < 4; l31 = l31 + 1) {
			fRec33_perm[l31] = 0.0f;
		}
		for (int l32 = 0; l32 < 4; l32 = l32 + 1) {
			fRec32_perm[l32] = 0.0f;
		}
		for (int l33 = 0; l33 < 4; l33 = l33 + 1) {
			fRec31_perm[l33] = 0.0f;
		}
		for (int l34 = 0; l34 < 4; l34 = l34 + 1) {
			fRec30_perm[l34] = 0.0f;
		}
		for (int l35 = 0; l35 < 4; l35 = l35 + 1) {
			fRec39_perm[l35] = 0.0f;
		}
		for (int l36 = 0; l36 < 4; l36 = l36 + 1) {
			fRec38_perm[l36] = 0.0f;
		}
		for (int l37 = 0; l37 < 4; l37 = l37 + 1) {
			fRec37_perm[l37] = 0.0f;
		}
		for (int l38 = 0; l38 < 4; l38 = l38 + 1) {
			fRec36_perm[l38] = 0.0f;
		}
		for (int l39 = 0; l39 < 4; l39 = l39 + 1) {
			fRec35_perm[l39] = 0.0f;
		}
		for (int l40 = 0; l40 < 4; l40 = l40 + 1) {
			fRec44_perm[l40] = 0.0f;
		}
		for (int l41 = 0; l41 < 4; l41 = l41 + 1) {
			fRec43_perm[l41] = 0.0f;
		}
		for (int l42 = 0; l42 < 4; l42 = l42 + 1) {
			fRec42_perm[l42] = 0.0f;
		}
		for (int l43 = 0; l43 < 4; l43 = l43 + 1) {
			fRec41_perm[l43] = 0.0f;
		}
		for (int l44 = 0; l44 < 4; l44 = l44 + 1) {
			fRec40_perm[l44] = 0.0f;
		}
		for (int l45 = 0; l45 < 4; l45 = l45 + 1) {
			fRec49_perm[l45] = 0.0f;
		}
		for (int l46 = 0; l46 < 4; l46 = l46 + 1) {
			fRec48_perm[l46] = 0.0f;
		}
		for (int l47 = 0; l47 < 4; l47 = l47 + 1) {
			fRec47_perm[l47] = 0.0f;
		}
		for (int l48 = 0; l48 < 4; l48 = l48 + 1) {
			fRec46_perm[l48] = 0.0f;
		}
		for (int l49 = 0; l49 < 4; l49 = l49 + 1) {
			fRec45_perm[l49] = 0.0f;
		}
		for (int l50 = 0; l50 < 4; l50 = l50 + 1) {
			fRec54_perm[l50] = 0.0f;
		}
		for (int l51 = 0; l51 < 4; l51 = l51 + 1) {
			fRec53_perm[l51] = 0.0f;
		}
		for (int l52 = 0; l52 < 4; l52 = l52 + 1) {
			fRec52_perm[l52] = 0.0f;
		}
		for (int l53 = 0; l53 < 4; l53 = l53 + 1) {
			fRec51_perm[l53] = 0.0f;
		}
		for (int l54 = 0; l54 < 4; l54 = l54 + 1) {
			fRec50_perm[l54] = 0.0f;
		}
		for (int l55 = 0; l55 < 4; l55 = l55 + 1) {
			fRec59_perm[l55] = 0.0f;
		}
		for (int l56 = 0; l56 < 4; l56 = l56 + 1) {
			fRec58_perm[l56] = 0.0f;
		}
		for (int l57 = 0; l57 < 4; l57 = l57 + 1) {
			fRec57_perm[l57] = 0.0f;
		}
		for (int l58 = 0; l58 < 4; l58 = l58 + 1) {
			fRec56_perm[l58] = 0.0f;
		}
		for (int l59 = 0; l59 < 4; l59 = l59 + 1) {
			fRec55_perm[l59] = 0.0f;
		}
		for (int l60 = 0; l60 < 4; l60 = l60 + 1) {
			fRec64_perm[l60] = 0.0f;
		}
		for (int l61 = 0; l61 < 4; l61 = l61 + 1) {
			fRec63_perm[l61] = 0.0f;
		}
		for (int l62 = 0; l62 < 4; l62 = l62 + 1) {
			fRec62_perm[l62] = 0.0f;
		}
		for (int l63 = 0; l63 < 4; l63 = l63 + 1) {
			fRec61_perm[l63] = 0.0f;
		}
		for (int l64 = 0; l64 < 4; l64 = l64 + 1) {
			fRec60_perm[l64] = 0.0f;
		}
		for (int l65 = 0; l65 < 4; l65 = l65 + 1) {
			fRec69_perm[l65] = 0.0f;
		}
		for (int l66 = 0; l66 < 4; l66 = l66 + 1) {
			fRec68_perm[l66] = 0.0f;
		}
		for (int l67 = 0; l67 < 4; l67 = l67 + 1) {
			fRec67_perm[l67] = 0.0f;
		}
		for (int l68 = 0; l68 < 4; l68 = l68 + 1) {
			fRec66_perm[l68] = 0.0f;
		}
		for (int l69 = 0; l69 < 4; l69 = l69 + 1) {
			fRec65_perm[l69] = 0.0f;
		}
		for (int l70 = 0; l70 < 4; l70 = l70 + 1) {
			fRec74_perm[l70] = 0.0f;
		}
		for (int l71 = 0; l71 < 4; l71 = l71 + 1) {
			fRec73_perm[l71] = 0.0f;
		}
		for (int l72 = 0; l72 < 4; l72 = l72 + 1) {
			fRec72_perm[l72] = 0.0f;
		}
		for (int l73 = 0; l73 < 4; l73 = l73 + 1) {
			fRec71_perm[l73] = 0.0f;
		}
		for (int l74 = 0; l74 < 4; l74 = l74 + 1) {
			fRec70_perm[l74] = 0.0f;
		}
		for (int l75 = 0; l75 < 4; l75 = l75 + 1) {
			fRec79_perm[l75] = 0.0f;
		}
		for (int l76 = 0; l76 < 4; l76 = l76 + 1) {
			fRec78_perm[l76] = 0.0f;
		}
		for (int l77 = 0; l77 < 4; l77 = l77 + 1) {
			fRec77_perm[l77] = 0.0f;
		}
		for (int l78 = 0; l78 < 4; l78 = l78 + 1) {
			fRec76_perm[l78] = 0.0f;
		}
		for (int l79 = 0; l79 < 4; l79 = l79 + 1) {
			fRec75_perm[l79] = 0.0f;
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
	
	virtual heinzel_ladder16_vec* clone() {
		return new heinzel_ladder16_vec(*this);
	}
	
	virtual int getSampleRate() {
		return fSampleRate;
	}
	
	virtual void buildUserInterface(UI* ui_interface) {
		ui_interface->openVerticalBox("ladder16");
		ui_interface->closeBox();
	}
	
	virtual void compute(int count, FAUSTFLOAT** RESTRICT inputs, FAUSTFLOAT** RESTRICT outputs) {
		FAUSTFLOAT* input0_ptr = inputs[0];
		FAUSTFLOAT* input1_ptr = inputs[1];
		FAUSTFLOAT* input2_ptr = inputs[2];
		FAUSTFLOAT* input3_ptr = inputs[3];
		FAUSTFLOAT* input4_ptr = inputs[4];
		FAUSTFLOAT* input5_ptr = inputs[5];
		FAUSTFLOAT* input6_ptr = inputs[6];
		FAUSTFLOAT* input7_ptr = inputs[7];
		FAUSTFLOAT* input8_ptr = inputs[8];
		FAUSTFLOAT* input9_ptr = inputs[9];
		FAUSTFLOAT* input10_ptr = inputs[10];
		FAUSTFLOAT* input11_ptr = inputs[11];
		FAUSTFLOAT* input12_ptr = inputs[12];
		FAUSTFLOAT* input13_ptr = inputs[13];
		FAUSTFLOAT* input14_ptr = inputs[14];
		FAUSTFLOAT* input15_ptr = inputs[15];
		FAUSTFLOAT* output0_ptr = outputs[0];
		FAUSTFLOAT* output1_ptr = outputs[1];
		FAUSTFLOAT* output2_ptr = outputs[2];
		FAUSTFLOAT* output3_ptr = outputs[3];
		FAUSTFLOAT* output4_ptr = outputs[4];
		FAUSTFLOAT* output5_ptr = outputs[5];
		FAUSTFLOAT* output6_ptr = outputs[6];
		FAUSTFLOAT* output7_ptr = outputs[7];
		FAUSTFLOAT* output8_ptr = outputs[8];
		FAUSTFLOAT* output9_ptr = outputs[9];
		FAUSTFLOAT* output10_ptr = outputs[10];
		FAUSTFLOAT* output11_ptr = outputs[11];
		FAUSTFLOAT* output12_ptr = outputs[12];
		FAUSTFLOAT* output13_ptr = outputs[13];
		FAUSTFLOAT* output14_ptr = outputs[14];
		FAUSTFLOAT* output15_ptr = outputs[15];
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
		float fRec9_tmp[36];
		float* fRec9 = &fRec9_tmp[4];
		float fRec8_tmp[36];
		float* fRec8 = &fRec8_tmp[4];
		float fRec7_tmp[36];
		float* fRec7 = &fRec7_tmp[4];
		float fRec6_tmp[36];
		float* fRec6 = &fRec6_tmp[4];
		float fRec5_tmp[36];
		float* fRec5 = &fRec5_tmp[4];
		float fRec14_tmp[36];
		float* fRec14 = &fRec14_tmp[4];
		float fRec13_tmp[36];
		float* fRec13 = &fRec13_tmp[4];
		float fRec12_tmp[36];
		float* fRec12 = &fRec12_tmp[4];
		float fRec11_tmp[36];
		float* fRec11 = &fRec11_tmp[4];
		float fRec10_tmp[36];
		float* fRec10 = &fRec10_tmp[4];
		float fRec19_tmp[36];
		float* fRec19 = &fRec19_tmp[4];
		float fRec18_tmp[36];
		float* fRec18 = &fRec18_tmp[4];
		float fRec17_tmp[36];
		float* fRec17 = &fRec17_tmp[4];
		float fRec16_tmp[36];
		float* fRec16 = &fRec16_tmp[4];
		float fRec15_tmp[36];
		float* fRec15 = &fRec15_tmp[4];
		float fRec24_tmp[36];
		float* fRec24 = &fRec24_tmp[4];
		float fRec23_tmp[36];
		float* fRec23 = &fRec23_tmp[4];
		float fRec22_tmp[36];
		float* fRec22 = &fRec22_tmp[4];
		float fRec21_tmp[36];
		float* fRec21 = &fRec21_tmp[4];
		float fRec20_tmp[36];
		float* fRec20 = &fRec20_tmp[4];
		float fRec29_tmp[36];
		float* fRec29 = &fRec29_tmp[4];
		float fRec28_tmp[36];
		float* fRec28 = &fRec28_tmp[4];
		float fRec27_tmp[36];
		float* fRec27 = &fRec27_tmp[4];
		float fRec26_tmp[36];
		float* fRec26 = &fRec26_tmp[4];
		float fRec25_tmp[36];
		float* fRec25 = &fRec25_tmp[4];
		float fRec34_tmp[36];
		float* fRec34 = &fRec34_tmp[4];
		float fRec33_tmp[36];
		float* fRec33 = &fRec33_tmp[4];
		float fRec32_tmp[36];
		float* fRec32 = &fRec32_tmp[4];
		float fRec31_tmp[36];
		float* fRec31 = &fRec31_tmp[4];
		float fRec30_tmp[36];
		float* fRec30 = &fRec30_tmp[4];
		float fRec39_tmp[36];
		float* fRec39 = &fRec39_tmp[4];
		float fRec38_tmp[36];
		float* fRec38 = &fRec38_tmp[4];
		float fRec37_tmp[36];
		float* fRec37 = &fRec37_tmp[4];
		float fRec36_tmp[36];
		float* fRec36 = &fRec36_tmp[4];
		float fRec35_tmp[36];
		float* fRec35 = &fRec35_tmp[4];
		float fRec44_tmp[36];
		float* fRec44 = &fRec44_tmp[4];
		float fRec43_tmp[36];
		float* fRec43 = &fRec43_tmp[4];
		float fRec42_tmp[36];
		float* fRec42 = &fRec42_tmp[4];
		float fRec41_tmp[36];
		float* fRec41 = &fRec41_tmp[4];
		float fRec40_tmp[36];
		float* fRec40 = &fRec40_tmp[4];
		float fRec49_tmp[36];
		float* fRec49 = &fRec49_tmp[4];
		float fRec48_tmp[36];
		float* fRec48 = &fRec48_tmp[4];
		float fRec47_tmp[36];
		float* fRec47 = &fRec47_tmp[4];
		float fRec46_tmp[36];
		float* fRec46 = &fRec46_tmp[4];
		float fRec45_tmp[36];
		float* fRec45 = &fRec45_tmp[4];
		float fRec54_tmp[36];
		float* fRec54 = &fRec54_tmp[4];
		float fRec53_tmp[36];
		float* fRec53 = &fRec53_tmp[4];
		float fRec52_tmp[36];
		float* fRec52 = &fRec52_tmp[4];
		float fRec51_tmp[36];
		float* fRec51 = &fRec51_tmp[4];
		float fRec50_tmp[36];
		float* fRec50 = &fRec50_tmp[4];
		float fRec59_tmp[36];
		float* fRec59 = &fRec59_tmp[4];
		float fRec58_tmp[36];
		float* fRec58 = &fRec58_tmp[4];
		float fRec57_tmp[36];
		float* fRec57 = &fRec57_tmp[4];
		float fRec56_tmp[36];
		float* fRec56 = &fRec56_tmp[4];
		float fRec55_tmp[36];
		float* fRec55 = &fRec55_tmp[4];
		float fRec64_tmp[36];
		float* fRec64 = &fRec64_tmp[4];
		float fRec63_tmp[36];
		float* fRec63 = &fRec63_tmp[4];
		float fRec62_tmp[36];
		float* fRec62 = &fRec62_tmp[4];
		float fRec61_tmp[36];
		float* fRec61 = &fRec61_tmp[4];
		float fRec60_tmp[36];
		float* fRec60 = &fRec60_tmp[4];
		float fRec69_tmp[36];
		float* fRec69 = &fRec69_tmp[4];
		float fRec68_tmp[36];
		float* fRec68 = &fRec68_tmp[4];
		float fRec67_tmp[36];
		float* fRec67 = &fRec67_tmp[4];
		float fRec66_tmp[36];
		float* fRec66 = &fRec66_tmp[4];
		float fRec65_tmp[36];
		float* fRec65 = &fRec65_tmp[4];
		float fRec74_tmp[36];
		float* fRec74 = &fRec74_tmp[4];
		float fRec73_tmp[36];
		float* fRec73 = &fRec73_tmp[4];
		float fRec72_tmp[36];
		float* fRec72 = &fRec72_tmp[4];
		float fRec71_tmp[36];
		float* fRec71 = &fRec71_tmp[4];
		float fRec70_tmp[36];
		float* fRec70 = &fRec70_tmp[4];
		float fRec79_tmp[36];
		float* fRec79 = &fRec79_tmp[4];
		float fRec78_tmp[36];
		float* fRec78 = &fRec78_tmp[4];
		float fRec77_tmp[36];
		float* fRec77 = &fRec77_tmp[4];
		float fRec76_tmp[36];
		float* fRec76 = &fRec76_tmp[4];
		float fRec75_tmp[36];
		float* fRec75 = &fRec75_tmp[4];
		int vindex = 0;
		/* Main loop */
		for (vindex = 0; vindex <= (count - 32); vindex = vindex + 32) {
			FAUSTFLOAT* input0 = &input0_ptr[vindex];
			FAUSTFLOAT* input1 = &input1_ptr[vindex];
			FAUSTFLOAT* input2 = &input2_ptr[vindex];
			FAUSTFLOAT* input3 = &input3_ptr[vindex];
			FAUSTFLOAT* input4 = &input4_ptr[vindex];
			FAUSTFLOAT* input5 = &input5_ptr[vindex];
			FAUSTFLOAT* input6 = &input6_ptr[vindex];
			FAUSTFLOAT* input7 = &input7_ptr[vindex];
			FAUSTFLOAT* input8 = &input8_ptr[vindex];
			FAUSTFLOAT* input9 = &input9_ptr[vindex];
			FAUSTFLOAT* input10 = &input10_ptr[vindex];
			FAUSTFLOAT* input11 = &input11_ptr[vindex];
			FAUSTFLOAT* input12 = &input12_ptr[vindex];
			FAUSTFLOAT* input13 = &input13_ptr[vindex];
			FAUSTFLOAT* input14 = &input14_ptr[vindex];
			FAUSTFLOAT* input15 = &input15_ptr[vindex];
			FAUSTFLOAT* output0 = &output0_ptr[vindex];
			FAUSTFLOAT* output1 = &output1_ptr[vindex];
			FAUSTFLOAT* output2 = &output2_ptr[vindex];
			FAUSTFLOAT* output3 = &output3_ptr[vindex];
			FAUSTFLOAT* output4 = &output4_ptr[vindex];
			FAUSTFLOAT* output5 = &output5_ptr[vindex];
			FAUSTFLOAT* output6 = &output6_ptr[vindex];
			FAUSTFLOAT* output7 = &output7_ptr[vindex];
			FAUSTFLOAT* output8 = &output8_ptr[vindex];
			FAUSTFLOAT* output9 = &output9_ptr[vindex];
			FAUSTFLOAT* output10 = &output10_ptr[vindex];
			FAUSTFLOAT* output11 = &output11_ptr[vindex];
			FAUSTFLOAT* output12 = &output12_ptr[vindex];
			FAUSTFLOAT* output13 = &output13_ptr[vindex];
			FAUSTFLOAT* output14 = &output14_ptr[vindex];
			FAUSTFLOAT* output15 = &output15_ptr[vindex];
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
			/* Recursive loop 1 */
			/* Pre code */
			for (int j10 = 0; j10 < 4; j10 = j10 + 1) {
				fRec9_tmp[j10] = fRec9_perm[j10];
			}
			for (int j12 = 0; j12 < 4; j12 = j12 + 1) {
				fRec8_tmp[j12] = fRec8_perm[j12];
			}
			for (int j14 = 0; j14 < 4; j14 = j14 + 1) {
				fRec7_tmp[j14] = fRec7_perm[j14];
			}
			for (int j16 = 0; j16 < 4; j16 = j16 + 1) {
				fRec6_tmp[j16] = fRec6_perm[j16];
			}
			for (int j18 = 0; j18 < 4; j18 = j18 + 1) {
				fRec5_tmp[j18] = fRec5_perm[j18];
			}
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				fRec9[i] = static_cast<float>(input1[i]) + fConst1 * fRec9[i - 1] - 2.8f * fRec5[i - 1];
				fRec8[i] = fRec9[i] + fConst1 * fRec8[i - 1];
				fRec7[i] = fRec8[i] + fConst1 * fRec7[i - 1];
				fRec6[i] = fRec7[i] + fConst1 * fRec6[i - 1];
				fRec5[i] = fConst2 * fRec6[i];
			}
			/* Post code */
			for (int j11 = 0; j11 < 4; j11 = j11 + 1) {
				fRec9_perm[j11] = fRec9_tmp[vsize + j11];
			}
			for (int j13 = 0; j13 < 4; j13 = j13 + 1) {
				fRec8_perm[j13] = fRec8_tmp[vsize + j13];
			}
			for (int j15 = 0; j15 < 4; j15 = j15 + 1) {
				fRec7_perm[j15] = fRec7_tmp[vsize + j15];
			}
			for (int j17 = 0; j17 < 4; j17 = j17 + 1) {
				fRec6_perm[j17] = fRec6_tmp[vsize + j17];
			}
			for (int j19 = 0; j19 < 4; j19 = j19 + 1) {
				fRec5_perm[j19] = fRec5_tmp[vsize + j19];
			}
			/* Recursive loop 2 */
			/* Pre code */
			for (int j20 = 0; j20 < 4; j20 = j20 + 1) {
				fRec14_tmp[j20] = fRec14_perm[j20];
			}
			for (int j22 = 0; j22 < 4; j22 = j22 + 1) {
				fRec13_tmp[j22] = fRec13_perm[j22];
			}
			for (int j24 = 0; j24 < 4; j24 = j24 + 1) {
				fRec12_tmp[j24] = fRec12_perm[j24];
			}
			for (int j26 = 0; j26 < 4; j26 = j26 + 1) {
				fRec11_tmp[j26] = fRec11_perm[j26];
			}
			for (int j28 = 0; j28 < 4; j28 = j28 + 1) {
				fRec10_tmp[j28] = fRec10_perm[j28];
			}
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				fRec14[i] = static_cast<float>(input2[i]) + fConst1 * fRec14[i - 1] - 2.8f * fRec10[i - 1];
				fRec13[i] = fRec14[i] + fConst1 * fRec13[i - 1];
				fRec12[i] = fRec13[i] + fConst1 * fRec12[i - 1];
				fRec11[i] = fRec12[i] + fConst1 * fRec11[i - 1];
				fRec10[i] = fConst2 * fRec11[i];
			}
			/* Post code */
			for (int j21 = 0; j21 < 4; j21 = j21 + 1) {
				fRec14_perm[j21] = fRec14_tmp[vsize + j21];
			}
			for (int j23 = 0; j23 < 4; j23 = j23 + 1) {
				fRec13_perm[j23] = fRec13_tmp[vsize + j23];
			}
			for (int j25 = 0; j25 < 4; j25 = j25 + 1) {
				fRec12_perm[j25] = fRec12_tmp[vsize + j25];
			}
			for (int j27 = 0; j27 < 4; j27 = j27 + 1) {
				fRec11_perm[j27] = fRec11_tmp[vsize + j27];
			}
			for (int j29 = 0; j29 < 4; j29 = j29 + 1) {
				fRec10_perm[j29] = fRec10_tmp[vsize + j29];
			}
			/* Recursive loop 3 */
			/* Pre code */
			for (int j30 = 0; j30 < 4; j30 = j30 + 1) {
				fRec19_tmp[j30] = fRec19_perm[j30];
			}
			for (int j32 = 0; j32 < 4; j32 = j32 + 1) {
				fRec18_tmp[j32] = fRec18_perm[j32];
			}
			for (int j34 = 0; j34 < 4; j34 = j34 + 1) {
				fRec17_tmp[j34] = fRec17_perm[j34];
			}
			for (int j36 = 0; j36 < 4; j36 = j36 + 1) {
				fRec16_tmp[j36] = fRec16_perm[j36];
			}
			for (int j38 = 0; j38 < 4; j38 = j38 + 1) {
				fRec15_tmp[j38] = fRec15_perm[j38];
			}
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				fRec19[i] = static_cast<float>(input3[i]) + fConst1 * fRec19[i - 1] - 2.8f * fRec15[i - 1];
				fRec18[i] = fRec19[i] + fConst1 * fRec18[i - 1];
				fRec17[i] = fRec18[i] + fConst1 * fRec17[i - 1];
				fRec16[i] = fRec17[i] + fConst1 * fRec16[i - 1];
				fRec15[i] = fConst2 * fRec16[i];
			}
			/* Post code */
			for (int j31 = 0; j31 < 4; j31 = j31 + 1) {
				fRec19_perm[j31] = fRec19_tmp[vsize + j31];
			}
			for (int j33 = 0; j33 < 4; j33 = j33 + 1) {
				fRec18_perm[j33] = fRec18_tmp[vsize + j33];
			}
			for (int j35 = 0; j35 < 4; j35 = j35 + 1) {
				fRec17_perm[j35] = fRec17_tmp[vsize + j35];
			}
			for (int j37 = 0; j37 < 4; j37 = j37 + 1) {
				fRec16_perm[j37] = fRec16_tmp[vsize + j37];
			}
			for (int j39 = 0; j39 < 4; j39 = j39 + 1) {
				fRec15_perm[j39] = fRec15_tmp[vsize + j39];
			}
			/* Recursive loop 4 */
			/* Pre code */
			for (int j40 = 0; j40 < 4; j40 = j40 + 1) {
				fRec24_tmp[j40] = fRec24_perm[j40];
			}
			for (int j42 = 0; j42 < 4; j42 = j42 + 1) {
				fRec23_tmp[j42] = fRec23_perm[j42];
			}
			for (int j44 = 0; j44 < 4; j44 = j44 + 1) {
				fRec22_tmp[j44] = fRec22_perm[j44];
			}
			for (int j46 = 0; j46 < 4; j46 = j46 + 1) {
				fRec21_tmp[j46] = fRec21_perm[j46];
			}
			for (int j48 = 0; j48 < 4; j48 = j48 + 1) {
				fRec20_tmp[j48] = fRec20_perm[j48];
			}
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				fRec24[i] = static_cast<float>(input4[i]) + fConst1 * fRec24[i - 1] - 2.8f * fRec20[i - 1];
				fRec23[i] = fRec24[i] + fConst1 * fRec23[i - 1];
				fRec22[i] = fRec23[i] + fConst1 * fRec22[i - 1];
				fRec21[i] = fRec22[i] + fConst1 * fRec21[i - 1];
				fRec20[i] = fConst2 * fRec21[i];
			}
			/* Post code */
			for (int j41 = 0; j41 < 4; j41 = j41 + 1) {
				fRec24_perm[j41] = fRec24_tmp[vsize + j41];
			}
			for (int j43 = 0; j43 < 4; j43 = j43 + 1) {
				fRec23_perm[j43] = fRec23_tmp[vsize + j43];
			}
			for (int j45 = 0; j45 < 4; j45 = j45 + 1) {
				fRec22_perm[j45] = fRec22_tmp[vsize + j45];
			}
			for (int j47 = 0; j47 < 4; j47 = j47 + 1) {
				fRec21_perm[j47] = fRec21_tmp[vsize + j47];
			}
			for (int j49 = 0; j49 < 4; j49 = j49 + 1) {
				fRec20_perm[j49] = fRec20_tmp[vsize + j49];
			}
			/* Recursive loop 5 */
			/* Pre code */
			for (int j50 = 0; j50 < 4; j50 = j50 + 1) {
				fRec29_tmp[j50] = fRec29_perm[j50];
			}
			for (int j52 = 0; j52 < 4; j52 = j52 + 1) {
				fRec28_tmp[j52] = fRec28_perm[j52];
			}
			for (int j54 = 0; j54 < 4; j54 = j54 + 1) {
				fRec27_tmp[j54] = fRec27_perm[j54];
			}
			for (int j56 = 0; j56 < 4; j56 = j56 + 1) {
				fRec26_tmp[j56] = fRec26_perm[j56];
			}
			for (int j58 = 0; j58 < 4; j58 = j58 + 1) {
				fRec25_tmp[j58] = fRec25_perm[j58];
			}
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				fRec29[i] = static_cast<float>(input5[i]) + fConst1 * fRec29[i - 1] - 2.8f * fRec25[i - 1];
				fRec28[i] = fRec29[i] + fConst1 * fRec28[i - 1];
				fRec27[i] = fRec28[i] + fConst1 * fRec27[i - 1];
				fRec26[i] = fRec27[i] + fConst1 * fRec26[i - 1];
				fRec25[i] = fConst2 * fRec26[i];
			}
			/* Post code */
			for (int j51 = 0; j51 < 4; j51 = j51 + 1) {
				fRec29_perm[j51] = fRec29_tmp[vsize + j51];
			}
			for (int j53 = 0; j53 < 4; j53 = j53 + 1) {
				fRec28_perm[j53] = fRec28_tmp[vsize + j53];
			}
			for (int j55 = 0; j55 < 4; j55 = j55 + 1) {
				fRec27_perm[j55] = fRec27_tmp[vsize + j55];
			}
			for (int j57 = 0; j57 < 4; j57 = j57 + 1) {
				fRec26_perm[j57] = fRec26_tmp[vsize + j57];
			}
			for (int j59 = 0; j59 < 4; j59 = j59 + 1) {
				fRec25_perm[j59] = fRec25_tmp[vsize + j59];
			}
			/* Recursive loop 6 */
			/* Pre code */
			for (int j60 = 0; j60 < 4; j60 = j60 + 1) {
				fRec34_tmp[j60] = fRec34_perm[j60];
			}
			for (int j62 = 0; j62 < 4; j62 = j62 + 1) {
				fRec33_tmp[j62] = fRec33_perm[j62];
			}
			for (int j64 = 0; j64 < 4; j64 = j64 + 1) {
				fRec32_tmp[j64] = fRec32_perm[j64];
			}
			for (int j66 = 0; j66 < 4; j66 = j66 + 1) {
				fRec31_tmp[j66] = fRec31_perm[j66];
			}
			for (int j68 = 0; j68 < 4; j68 = j68 + 1) {
				fRec30_tmp[j68] = fRec30_perm[j68];
			}
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				fRec34[i] = static_cast<float>(input6[i]) + fConst1 * fRec34[i - 1] - 2.8f * fRec30[i - 1];
				fRec33[i] = fRec34[i] + fConst1 * fRec33[i - 1];
				fRec32[i] = fRec33[i] + fConst1 * fRec32[i - 1];
				fRec31[i] = fRec32[i] + fConst1 * fRec31[i - 1];
				fRec30[i] = fConst2 * fRec31[i];
			}
			/* Post code */
			for (int j61 = 0; j61 < 4; j61 = j61 + 1) {
				fRec34_perm[j61] = fRec34_tmp[vsize + j61];
			}
			for (int j63 = 0; j63 < 4; j63 = j63 + 1) {
				fRec33_perm[j63] = fRec33_tmp[vsize + j63];
			}
			for (int j65 = 0; j65 < 4; j65 = j65 + 1) {
				fRec32_perm[j65] = fRec32_tmp[vsize + j65];
			}
			for (int j67 = 0; j67 < 4; j67 = j67 + 1) {
				fRec31_perm[j67] = fRec31_tmp[vsize + j67];
			}
			for (int j69 = 0; j69 < 4; j69 = j69 + 1) {
				fRec30_perm[j69] = fRec30_tmp[vsize + j69];
			}
			/* Recursive loop 7 */
			/* Pre code */
			for (int j70 = 0; j70 < 4; j70 = j70 + 1) {
				fRec39_tmp[j70] = fRec39_perm[j70];
			}
			for (int j72 = 0; j72 < 4; j72 = j72 + 1) {
				fRec38_tmp[j72] = fRec38_perm[j72];
			}
			for (int j74 = 0; j74 < 4; j74 = j74 + 1) {
				fRec37_tmp[j74] = fRec37_perm[j74];
			}
			for (int j76 = 0; j76 < 4; j76 = j76 + 1) {
				fRec36_tmp[j76] = fRec36_perm[j76];
			}
			for (int j78 = 0; j78 < 4; j78 = j78 + 1) {
				fRec35_tmp[j78] = fRec35_perm[j78];
			}
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				fRec39[i] = static_cast<float>(input7[i]) + fConst1 * fRec39[i - 1] - 2.8f * fRec35[i - 1];
				fRec38[i] = fRec39[i] + fConst1 * fRec38[i - 1];
				fRec37[i] = fRec38[i] + fConst1 * fRec37[i - 1];
				fRec36[i] = fRec37[i] + fConst1 * fRec36[i - 1];
				fRec35[i] = fConst2 * fRec36[i];
			}
			/* Post code */
			for (int j71 = 0; j71 < 4; j71 = j71 + 1) {
				fRec39_perm[j71] = fRec39_tmp[vsize + j71];
			}
			for (int j73 = 0; j73 < 4; j73 = j73 + 1) {
				fRec38_perm[j73] = fRec38_tmp[vsize + j73];
			}
			for (int j75 = 0; j75 < 4; j75 = j75 + 1) {
				fRec37_perm[j75] = fRec37_tmp[vsize + j75];
			}
			for (int j77 = 0; j77 < 4; j77 = j77 + 1) {
				fRec36_perm[j77] = fRec36_tmp[vsize + j77];
			}
			for (int j79 = 0; j79 < 4; j79 = j79 + 1) {
				fRec35_perm[j79] = fRec35_tmp[vsize + j79];
			}
			/* Recursive loop 8 */
			/* Pre code */
			for (int j80 = 0; j80 < 4; j80 = j80 + 1) {
				fRec44_tmp[j80] = fRec44_perm[j80];
			}
			for (int j82 = 0; j82 < 4; j82 = j82 + 1) {
				fRec43_tmp[j82] = fRec43_perm[j82];
			}
			for (int j84 = 0; j84 < 4; j84 = j84 + 1) {
				fRec42_tmp[j84] = fRec42_perm[j84];
			}
			for (int j86 = 0; j86 < 4; j86 = j86 + 1) {
				fRec41_tmp[j86] = fRec41_perm[j86];
			}
			for (int j88 = 0; j88 < 4; j88 = j88 + 1) {
				fRec40_tmp[j88] = fRec40_perm[j88];
			}
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				fRec44[i] = static_cast<float>(input8[i]) + fConst1 * fRec44[i - 1] - 2.8f * fRec40[i - 1];
				fRec43[i] = fRec44[i] + fConst1 * fRec43[i - 1];
				fRec42[i] = fRec43[i] + fConst1 * fRec42[i - 1];
				fRec41[i] = fRec42[i] + fConst1 * fRec41[i - 1];
				fRec40[i] = fConst2 * fRec41[i];
			}
			/* Post code */
			for (int j81 = 0; j81 < 4; j81 = j81 + 1) {
				fRec44_perm[j81] = fRec44_tmp[vsize + j81];
			}
			for (int j83 = 0; j83 < 4; j83 = j83 + 1) {
				fRec43_perm[j83] = fRec43_tmp[vsize + j83];
			}
			for (int j85 = 0; j85 < 4; j85 = j85 + 1) {
				fRec42_perm[j85] = fRec42_tmp[vsize + j85];
			}
			for (int j87 = 0; j87 < 4; j87 = j87 + 1) {
				fRec41_perm[j87] = fRec41_tmp[vsize + j87];
			}
			for (int j89 = 0; j89 < 4; j89 = j89 + 1) {
				fRec40_perm[j89] = fRec40_tmp[vsize + j89];
			}
			/* Recursive loop 9 */
			/* Pre code */
			for (int j90 = 0; j90 < 4; j90 = j90 + 1) {
				fRec49_tmp[j90] = fRec49_perm[j90];
			}
			for (int j92 = 0; j92 < 4; j92 = j92 + 1) {
				fRec48_tmp[j92] = fRec48_perm[j92];
			}
			for (int j94 = 0; j94 < 4; j94 = j94 + 1) {
				fRec47_tmp[j94] = fRec47_perm[j94];
			}
			for (int j96 = 0; j96 < 4; j96 = j96 + 1) {
				fRec46_tmp[j96] = fRec46_perm[j96];
			}
			for (int j98 = 0; j98 < 4; j98 = j98 + 1) {
				fRec45_tmp[j98] = fRec45_perm[j98];
			}
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				fRec49[i] = static_cast<float>(input9[i]) + fConst1 * fRec49[i - 1] - 2.8f * fRec45[i - 1];
				fRec48[i] = fRec49[i] + fConst1 * fRec48[i - 1];
				fRec47[i] = fRec48[i] + fConst1 * fRec47[i - 1];
				fRec46[i] = fRec47[i] + fConst1 * fRec46[i - 1];
				fRec45[i] = fConst2 * fRec46[i];
			}
			/* Post code */
			for (int j91 = 0; j91 < 4; j91 = j91 + 1) {
				fRec49_perm[j91] = fRec49_tmp[vsize + j91];
			}
			for (int j93 = 0; j93 < 4; j93 = j93 + 1) {
				fRec48_perm[j93] = fRec48_tmp[vsize + j93];
			}
			for (int j95 = 0; j95 < 4; j95 = j95 + 1) {
				fRec47_perm[j95] = fRec47_tmp[vsize + j95];
			}
			for (int j97 = 0; j97 < 4; j97 = j97 + 1) {
				fRec46_perm[j97] = fRec46_tmp[vsize + j97];
			}
			for (int j99 = 0; j99 < 4; j99 = j99 + 1) {
				fRec45_perm[j99] = fRec45_tmp[vsize + j99];
			}
			/* Recursive loop 10 */
			/* Pre code */
			for (int j100 = 0; j100 < 4; j100 = j100 + 1) {
				fRec54_tmp[j100] = fRec54_perm[j100];
			}
			for (int j102 = 0; j102 < 4; j102 = j102 + 1) {
				fRec53_tmp[j102] = fRec53_perm[j102];
			}
			for (int j104 = 0; j104 < 4; j104 = j104 + 1) {
				fRec52_tmp[j104] = fRec52_perm[j104];
			}
			for (int j106 = 0; j106 < 4; j106 = j106 + 1) {
				fRec51_tmp[j106] = fRec51_perm[j106];
			}
			for (int j108 = 0; j108 < 4; j108 = j108 + 1) {
				fRec50_tmp[j108] = fRec50_perm[j108];
			}
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				fRec54[i] = static_cast<float>(input10[i]) + fConst1 * fRec54[i - 1] - 2.8f * fRec50[i - 1];
				fRec53[i] = fRec54[i] + fConst1 * fRec53[i - 1];
				fRec52[i] = fRec53[i] + fConst1 * fRec52[i - 1];
				fRec51[i] = fRec52[i] + fConst1 * fRec51[i - 1];
				fRec50[i] = fConst2 * fRec51[i];
			}
			/* Post code */
			for (int j101 = 0; j101 < 4; j101 = j101 + 1) {
				fRec54_perm[j101] = fRec54_tmp[vsize + j101];
			}
			for (int j103 = 0; j103 < 4; j103 = j103 + 1) {
				fRec53_perm[j103] = fRec53_tmp[vsize + j103];
			}
			for (int j105 = 0; j105 < 4; j105 = j105 + 1) {
				fRec52_perm[j105] = fRec52_tmp[vsize + j105];
			}
			for (int j107 = 0; j107 < 4; j107 = j107 + 1) {
				fRec51_perm[j107] = fRec51_tmp[vsize + j107];
			}
			for (int j109 = 0; j109 < 4; j109 = j109 + 1) {
				fRec50_perm[j109] = fRec50_tmp[vsize + j109];
			}
			/* Recursive loop 11 */
			/* Pre code */
			for (int j110 = 0; j110 < 4; j110 = j110 + 1) {
				fRec59_tmp[j110] = fRec59_perm[j110];
			}
			for (int j112 = 0; j112 < 4; j112 = j112 + 1) {
				fRec58_tmp[j112] = fRec58_perm[j112];
			}
			for (int j114 = 0; j114 < 4; j114 = j114 + 1) {
				fRec57_tmp[j114] = fRec57_perm[j114];
			}
			for (int j116 = 0; j116 < 4; j116 = j116 + 1) {
				fRec56_tmp[j116] = fRec56_perm[j116];
			}
			for (int j118 = 0; j118 < 4; j118 = j118 + 1) {
				fRec55_tmp[j118] = fRec55_perm[j118];
			}
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				fRec59[i] = static_cast<float>(input11[i]) + fConst1 * fRec59[i - 1] - 2.8f * fRec55[i - 1];
				fRec58[i] = fRec59[i] + fConst1 * fRec58[i - 1];
				fRec57[i] = fRec58[i] + fConst1 * fRec57[i - 1];
				fRec56[i] = fRec57[i] + fConst1 * fRec56[i - 1];
				fRec55[i] = fConst2 * fRec56[i];
			}
			/* Post code */
			for (int j111 = 0; j111 < 4; j111 = j111 + 1) {
				fRec59_perm[j111] = fRec59_tmp[vsize + j111];
			}
			for (int j113 = 0; j113 < 4; j113 = j113 + 1) {
				fRec58_perm[j113] = fRec58_tmp[vsize + j113];
			}
			for (int j115 = 0; j115 < 4; j115 = j115 + 1) {
				fRec57_perm[j115] = fRec57_tmp[vsize + j115];
			}
			for (int j117 = 0; j117 < 4; j117 = j117 + 1) {
				fRec56_perm[j117] = fRec56_tmp[vsize + j117];
			}
			for (int j119 = 0; j119 < 4; j119 = j119 + 1) {
				fRec55_perm[j119] = fRec55_tmp[vsize + j119];
			}
			/* Recursive loop 12 */
			/* Pre code */
			for (int j120 = 0; j120 < 4; j120 = j120 + 1) {
				fRec64_tmp[j120] = fRec64_perm[j120];
			}
			for (int j122 = 0; j122 < 4; j122 = j122 + 1) {
				fRec63_tmp[j122] = fRec63_perm[j122];
			}
			for (int j124 = 0; j124 < 4; j124 = j124 + 1) {
				fRec62_tmp[j124] = fRec62_perm[j124];
			}
			for (int j126 = 0; j126 < 4; j126 = j126 + 1) {
				fRec61_tmp[j126] = fRec61_perm[j126];
			}
			for (int j128 = 0; j128 < 4; j128 = j128 + 1) {
				fRec60_tmp[j128] = fRec60_perm[j128];
			}
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				fRec64[i] = static_cast<float>(input12[i]) + fConst1 * fRec64[i - 1] - 2.8f * fRec60[i - 1];
				fRec63[i] = fRec64[i] + fConst1 * fRec63[i - 1];
				fRec62[i] = fRec63[i] + fConst1 * fRec62[i - 1];
				fRec61[i] = fRec62[i] + fConst1 * fRec61[i - 1];
				fRec60[i] = fConst2 * fRec61[i];
			}
			/* Post code */
			for (int j121 = 0; j121 < 4; j121 = j121 + 1) {
				fRec64_perm[j121] = fRec64_tmp[vsize + j121];
			}
			for (int j123 = 0; j123 < 4; j123 = j123 + 1) {
				fRec63_perm[j123] = fRec63_tmp[vsize + j123];
			}
			for (int j125 = 0; j125 < 4; j125 = j125 + 1) {
				fRec62_perm[j125] = fRec62_tmp[vsize + j125];
			}
			for (int j127 = 0; j127 < 4; j127 = j127 + 1) {
				fRec61_perm[j127] = fRec61_tmp[vsize + j127];
			}
			for (int j129 = 0; j129 < 4; j129 = j129 + 1) {
				fRec60_perm[j129] = fRec60_tmp[vsize + j129];
			}
			/* Recursive loop 13 */
			/* Pre code */
			for (int j130 = 0; j130 < 4; j130 = j130 + 1) {
				fRec69_tmp[j130] = fRec69_perm[j130];
			}
			for (int j132 = 0; j132 < 4; j132 = j132 + 1) {
				fRec68_tmp[j132] = fRec68_perm[j132];
			}
			for (int j134 = 0; j134 < 4; j134 = j134 + 1) {
				fRec67_tmp[j134] = fRec67_perm[j134];
			}
			for (int j136 = 0; j136 < 4; j136 = j136 + 1) {
				fRec66_tmp[j136] = fRec66_perm[j136];
			}
			for (int j138 = 0; j138 < 4; j138 = j138 + 1) {
				fRec65_tmp[j138] = fRec65_perm[j138];
			}
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				fRec69[i] = static_cast<float>(input13[i]) + fConst1 * fRec69[i - 1] - 2.8f * fRec65[i - 1];
				fRec68[i] = fRec69[i] + fConst1 * fRec68[i - 1];
				fRec67[i] = fRec68[i] + fConst1 * fRec67[i - 1];
				fRec66[i] = fRec67[i] + fConst1 * fRec66[i - 1];
				fRec65[i] = fConst2 * fRec66[i];
			}
			/* Post code */
			for (int j131 = 0; j131 < 4; j131 = j131 + 1) {
				fRec69_perm[j131] = fRec69_tmp[vsize + j131];
			}
			for (int j133 = 0; j133 < 4; j133 = j133 + 1) {
				fRec68_perm[j133] = fRec68_tmp[vsize + j133];
			}
			for (int j135 = 0; j135 < 4; j135 = j135 + 1) {
				fRec67_perm[j135] = fRec67_tmp[vsize + j135];
			}
			for (int j137 = 0; j137 < 4; j137 = j137 + 1) {
				fRec66_perm[j137] = fRec66_tmp[vsize + j137];
			}
			for (int j139 = 0; j139 < 4; j139 = j139 + 1) {
				fRec65_perm[j139] = fRec65_tmp[vsize + j139];
			}
			/* Recursive loop 14 */
			/* Pre code */
			for (int j140 = 0; j140 < 4; j140 = j140 + 1) {
				fRec74_tmp[j140] = fRec74_perm[j140];
			}
			for (int j142 = 0; j142 < 4; j142 = j142 + 1) {
				fRec73_tmp[j142] = fRec73_perm[j142];
			}
			for (int j144 = 0; j144 < 4; j144 = j144 + 1) {
				fRec72_tmp[j144] = fRec72_perm[j144];
			}
			for (int j146 = 0; j146 < 4; j146 = j146 + 1) {
				fRec71_tmp[j146] = fRec71_perm[j146];
			}
			for (int j148 = 0; j148 < 4; j148 = j148 + 1) {
				fRec70_tmp[j148] = fRec70_perm[j148];
			}
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				fRec74[i] = static_cast<float>(input14[i]) + fConst1 * fRec74[i - 1] - 2.8f * fRec70[i - 1];
				fRec73[i] = fRec74[i] + fConst1 * fRec73[i - 1];
				fRec72[i] = fRec73[i] + fConst1 * fRec72[i - 1];
				fRec71[i] = fRec72[i] + fConst1 * fRec71[i - 1];
				fRec70[i] = fConst2 * fRec71[i];
			}
			/* Post code */
			for (int j141 = 0; j141 < 4; j141 = j141 + 1) {
				fRec74_perm[j141] = fRec74_tmp[vsize + j141];
			}
			for (int j143 = 0; j143 < 4; j143 = j143 + 1) {
				fRec73_perm[j143] = fRec73_tmp[vsize + j143];
			}
			for (int j145 = 0; j145 < 4; j145 = j145 + 1) {
				fRec72_perm[j145] = fRec72_tmp[vsize + j145];
			}
			for (int j147 = 0; j147 < 4; j147 = j147 + 1) {
				fRec71_perm[j147] = fRec71_tmp[vsize + j147];
			}
			for (int j149 = 0; j149 < 4; j149 = j149 + 1) {
				fRec70_perm[j149] = fRec70_tmp[vsize + j149];
			}
			/* Recursive loop 15 */
			/* Pre code */
			for (int j150 = 0; j150 < 4; j150 = j150 + 1) {
				fRec79_tmp[j150] = fRec79_perm[j150];
			}
			for (int j152 = 0; j152 < 4; j152 = j152 + 1) {
				fRec78_tmp[j152] = fRec78_perm[j152];
			}
			for (int j154 = 0; j154 < 4; j154 = j154 + 1) {
				fRec77_tmp[j154] = fRec77_perm[j154];
			}
			for (int j156 = 0; j156 < 4; j156 = j156 + 1) {
				fRec76_tmp[j156] = fRec76_perm[j156];
			}
			for (int j158 = 0; j158 < 4; j158 = j158 + 1) {
				fRec75_tmp[j158] = fRec75_perm[j158];
			}
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				fRec79[i] = static_cast<float>(input15[i]) + fConst1 * fRec79[i - 1] - 2.8f * fRec75[i - 1];
				fRec78[i] = fRec79[i] + fConst1 * fRec78[i - 1];
				fRec77[i] = fRec78[i] + fConst1 * fRec77[i - 1];
				fRec76[i] = fRec77[i] + fConst1 * fRec76[i - 1];
				fRec75[i] = fConst2 * fRec76[i];
			}
			/* Post code */
			for (int j151 = 0; j151 < 4; j151 = j151 + 1) {
				fRec79_perm[j151] = fRec79_tmp[vsize + j151];
			}
			for (int j153 = 0; j153 < 4; j153 = j153 + 1) {
				fRec78_perm[j153] = fRec78_tmp[vsize + j153];
			}
			for (int j155 = 0; j155 < 4; j155 = j155 + 1) {
				fRec77_perm[j155] = fRec77_tmp[vsize + j155];
			}
			for (int j157 = 0; j157 < 4; j157 = j157 + 1) {
				fRec76_perm[j157] = fRec76_tmp[vsize + j157];
			}
			for (int j159 = 0; j159 < 4; j159 = j159 + 1) {
				fRec75_perm[j159] = fRec75_tmp[vsize + j159];
			}
			/* Vectorizable loop 16 */
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				output0[i] = static_cast<FAUSTFLOAT>(fRec0[i]);
			}
			/* Vectorizable loop 17 */
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				output1[i] = static_cast<FAUSTFLOAT>(fRec5[i]);
			}
			/* Vectorizable loop 18 */
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				output2[i] = static_cast<FAUSTFLOAT>(fRec10[i]);
			}
			/* Vectorizable loop 19 */
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				output3[i] = static_cast<FAUSTFLOAT>(fRec15[i]);
			}
			/* Vectorizable loop 20 */
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				output4[i] = static_cast<FAUSTFLOAT>(fRec20[i]);
			}
			/* Vectorizable loop 21 */
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				output5[i] = static_cast<FAUSTFLOAT>(fRec25[i]);
			}
			/* Vectorizable loop 22 */
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				output6[i] = static_cast<FAUSTFLOAT>(fRec30[i]);
			}
			/* Vectorizable loop 23 */
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				output7[i] = static_cast<FAUSTFLOAT>(fRec35[i]);
			}
			/* Vectorizable loop 24 */
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				output8[i] = static_cast<FAUSTFLOAT>(fRec40[i]);
			}
			/* Vectorizable loop 25 */
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				output9[i] = static_cast<FAUSTFLOAT>(fRec45[i]);
			}
			/* Vectorizable loop 26 */
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				output10[i] = static_cast<FAUSTFLOAT>(fRec50[i]);
			}
			/* Vectorizable loop 27 */
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				output11[i] = static_cast<FAUSTFLOAT>(fRec55[i]);
			}
			/* Vectorizable loop 28 */
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				output12[i] = static_cast<FAUSTFLOAT>(fRec60[i]);
			}
			/* Vectorizable loop 29 */
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				output13[i] = static_cast<FAUSTFLOAT>(fRec65[i]);
			}
			/* Vectorizable loop 30 */
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				output14[i] = static_cast<FAUSTFLOAT>(fRec70[i]);
			}
			/* Vectorizable loop 31 */
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				output15[i] = static_cast<FAUSTFLOAT>(fRec75[i]);
			}
		}
		/* Remaining frames */
		if (vindex < count) {
			FAUSTFLOAT* input0 = &input0_ptr[vindex];
			FAUSTFLOAT* input1 = &input1_ptr[vindex];
			FAUSTFLOAT* input2 = &input2_ptr[vindex];
			FAUSTFLOAT* input3 = &input3_ptr[vindex];
			FAUSTFLOAT* input4 = &input4_ptr[vindex];
			FAUSTFLOAT* input5 = &input5_ptr[vindex];
			FAUSTFLOAT* input6 = &input6_ptr[vindex];
			FAUSTFLOAT* input7 = &input7_ptr[vindex];
			FAUSTFLOAT* input8 = &input8_ptr[vindex];
			FAUSTFLOAT* input9 = &input9_ptr[vindex];
			FAUSTFLOAT* input10 = &input10_ptr[vindex];
			FAUSTFLOAT* input11 = &input11_ptr[vindex];
			FAUSTFLOAT* input12 = &input12_ptr[vindex];
			FAUSTFLOAT* input13 = &input13_ptr[vindex];
			FAUSTFLOAT* input14 = &input14_ptr[vindex];
			FAUSTFLOAT* input15 = &input15_ptr[vindex];
			FAUSTFLOAT* output0 = &output0_ptr[vindex];
			FAUSTFLOAT* output1 = &output1_ptr[vindex];
			FAUSTFLOAT* output2 = &output2_ptr[vindex];
			FAUSTFLOAT* output3 = &output3_ptr[vindex];
			FAUSTFLOAT* output4 = &output4_ptr[vindex];
			FAUSTFLOAT* output5 = &output5_ptr[vindex];
			FAUSTFLOAT* output6 = &output6_ptr[vindex];
			FAUSTFLOAT* output7 = &output7_ptr[vindex];
			FAUSTFLOAT* output8 = &output8_ptr[vindex];
			FAUSTFLOAT* output9 = &output9_ptr[vindex];
			FAUSTFLOAT* output10 = &output10_ptr[vindex];
			FAUSTFLOAT* output11 = &output11_ptr[vindex];
			FAUSTFLOAT* output12 = &output12_ptr[vindex];
			FAUSTFLOAT* output13 = &output13_ptr[vindex];
			FAUSTFLOAT* output14 = &output14_ptr[vindex];
			FAUSTFLOAT* output15 = &output15_ptr[vindex];
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
			/* Recursive loop 1 */
			/* Pre code */
			for (int j10 = 0; j10 < 4; j10 = j10 + 1) {
				fRec9_tmp[j10] = fRec9_perm[j10];
			}
			for (int j12 = 0; j12 < 4; j12 = j12 + 1) {
				fRec8_tmp[j12] = fRec8_perm[j12];
			}
			for (int j14 = 0; j14 < 4; j14 = j14 + 1) {
				fRec7_tmp[j14] = fRec7_perm[j14];
			}
			for (int j16 = 0; j16 < 4; j16 = j16 + 1) {
				fRec6_tmp[j16] = fRec6_perm[j16];
			}
			for (int j18 = 0; j18 < 4; j18 = j18 + 1) {
				fRec5_tmp[j18] = fRec5_perm[j18];
			}
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				fRec9[i] = static_cast<float>(input1[i]) + fConst1 * fRec9[i - 1] - 2.8f * fRec5[i - 1];
				fRec8[i] = fRec9[i] + fConst1 * fRec8[i - 1];
				fRec7[i] = fRec8[i] + fConst1 * fRec7[i - 1];
				fRec6[i] = fRec7[i] + fConst1 * fRec6[i - 1];
				fRec5[i] = fConst2 * fRec6[i];
			}
			/* Post code */
			for (int j11 = 0; j11 < 4; j11 = j11 + 1) {
				fRec9_perm[j11] = fRec9_tmp[vsize + j11];
			}
			for (int j13 = 0; j13 < 4; j13 = j13 + 1) {
				fRec8_perm[j13] = fRec8_tmp[vsize + j13];
			}
			for (int j15 = 0; j15 < 4; j15 = j15 + 1) {
				fRec7_perm[j15] = fRec7_tmp[vsize + j15];
			}
			for (int j17 = 0; j17 < 4; j17 = j17 + 1) {
				fRec6_perm[j17] = fRec6_tmp[vsize + j17];
			}
			for (int j19 = 0; j19 < 4; j19 = j19 + 1) {
				fRec5_perm[j19] = fRec5_tmp[vsize + j19];
			}
			/* Recursive loop 2 */
			/* Pre code */
			for (int j20 = 0; j20 < 4; j20 = j20 + 1) {
				fRec14_tmp[j20] = fRec14_perm[j20];
			}
			for (int j22 = 0; j22 < 4; j22 = j22 + 1) {
				fRec13_tmp[j22] = fRec13_perm[j22];
			}
			for (int j24 = 0; j24 < 4; j24 = j24 + 1) {
				fRec12_tmp[j24] = fRec12_perm[j24];
			}
			for (int j26 = 0; j26 < 4; j26 = j26 + 1) {
				fRec11_tmp[j26] = fRec11_perm[j26];
			}
			for (int j28 = 0; j28 < 4; j28 = j28 + 1) {
				fRec10_tmp[j28] = fRec10_perm[j28];
			}
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				fRec14[i] = static_cast<float>(input2[i]) + fConst1 * fRec14[i - 1] - 2.8f * fRec10[i - 1];
				fRec13[i] = fRec14[i] + fConst1 * fRec13[i - 1];
				fRec12[i] = fRec13[i] + fConst1 * fRec12[i - 1];
				fRec11[i] = fRec12[i] + fConst1 * fRec11[i - 1];
				fRec10[i] = fConst2 * fRec11[i];
			}
			/* Post code */
			for (int j21 = 0; j21 < 4; j21 = j21 + 1) {
				fRec14_perm[j21] = fRec14_tmp[vsize + j21];
			}
			for (int j23 = 0; j23 < 4; j23 = j23 + 1) {
				fRec13_perm[j23] = fRec13_tmp[vsize + j23];
			}
			for (int j25 = 0; j25 < 4; j25 = j25 + 1) {
				fRec12_perm[j25] = fRec12_tmp[vsize + j25];
			}
			for (int j27 = 0; j27 < 4; j27 = j27 + 1) {
				fRec11_perm[j27] = fRec11_tmp[vsize + j27];
			}
			for (int j29 = 0; j29 < 4; j29 = j29 + 1) {
				fRec10_perm[j29] = fRec10_tmp[vsize + j29];
			}
			/* Recursive loop 3 */
			/* Pre code */
			for (int j30 = 0; j30 < 4; j30 = j30 + 1) {
				fRec19_tmp[j30] = fRec19_perm[j30];
			}
			for (int j32 = 0; j32 < 4; j32 = j32 + 1) {
				fRec18_tmp[j32] = fRec18_perm[j32];
			}
			for (int j34 = 0; j34 < 4; j34 = j34 + 1) {
				fRec17_tmp[j34] = fRec17_perm[j34];
			}
			for (int j36 = 0; j36 < 4; j36 = j36 + 1) {
				fRec16_tmp[j36] = fRec16_perm[j36];
			}
			for (int j38 = 0; j38 < 4; j38 = j38 + 1) {
				fRec15_tmp[j38] = fRec15_perm[j38];
			}
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				fRec19[i] = static_cast<float>(input3[i]) + fConst1 * fRec19[i - 1] - 2.8f * fRec15[i - 1];
				fRec18[i] = fRec19[i] + fConst1 * fRec18[i - 1];
				fRec17[i] = fRec18[i] + fConst1 * fRec17[i - 1];
				fRec16[i] = fRec17[i] + fConst1 * fRec16[i - 1];
				fRec15[i] = fConst2 * fRec16[i];
			}
			/* Post code */
			for (int j31 = 0; j31 < 4; j31 = j31 + 1) {
				fRec19_perm[j31] = fRec19_tmp[vsize + j31];
			}
			for (int j33 = 0; j33 < 4; j33 = j33 + 1) {
				fRec18_perm[j33] = fRec18_tmp[vsize + j33];
			}
			for (int j35 = 0; j35 < 4; j35 = j35 + 1) {
				fRec17_perm[j35] = fRec17_tmp[vsize + j35];
			}
			for (int j37 = 0; j37 < 4; j37 = j37 + 1) {
				fRec16_perm[j37] = fRec16_tmp[vsize + j37];
			}
			for (int j39 = 0; j39 < 4; j39 = j39 + 1) {
				fRec15_perm[j39] = fRec15_tmp[vsize + j39];
			}
			/* Recursive loop 4 */
			/* Pre code */
			for (int j40 = 0; j40 < 4; j40 = j40 + 1) {
				fRec24_tmp[j40] = fRec24_perm[j40];
			}
			for (int j42 = 0; j42 < 4; j42 = j42 + 1) {
				fRec23_tmp[j42] = fRec23_perm[j42];
			}
			for (int j44 = 0; j44 < 4; j44 = j44 + 1) {
				fRec22_tmp[j44] = fRec22_perm[j44];
			}
			for (int j46 = 0; j46 < 4; j46 = j46 + 1) {
				fRec21_tmp[j46] = fRec21_perm[j46];
			}
			for (int j48 = 0; j48 < 4; j48 = j48 + 1) {
				fRec20_tmp[j48] = fRec20_perm[j48];
			}
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				fRec24[i] = static_cast<float>(input4[i]) + fConst1 * fRec24[i - 1] - 2.8f * fRec20[i - 1];
				fRec23[i] = fRec24[i] + fConst1 * fRec23[i - 1];
				fRec22[i] = fRec23[i] + fConst1 * fRec22[i - 1];
				fRec21[i] = fRec22[i] + fConst1 * fRec21[i - 1];
				fRec20[i] = fConst2 * fRec21[i];
			}
			/* Post code */
			for (int j41 = 0; j41 < 4; j41 = j41 + 1) {
				fRec24_perm[j41] = fRec24_tmp[vsize + j41];
			}
			for (int j43 = 0; j43 < 4; j43 = j43 + 1) {
				fRec23_perm[j43] = fRec23_tmp[vsize + j43];
			}
			for (int j45 = 0; j45 < 4; j45 = j45 + 1) {
				fRec22_perm[j45] = fRec22_tmp[vsize + j45];
			}
			for (int j47 = 0; j47 < 4; j47 = j47 + 1) {
				fRec21_perm[j47] = fRec21_tmp[vsize + j47];
			}
			for (int j49 = 0; j49 < 4; j49 = j49 + 1) {
				fRec20_perm[j49] = fRec20_tmp[vsize + j49];
			}
			/* Recursive loop 5 */
			/* Pre code */
			for (int j50 = 0; j50 < 4; j50 = j50 + 1) {
				fRec29_tmp[j50] = fRec29_perm[j50];
			}
			for (int j52 = 0; j52 < 4; j52 = j52 + 1) {
				fRec28_tmp[j52] = fRec28_perm[j52];
			}
			for (int j54 = 0; j54 < 4; j54 = j54 + 1) {
				fRec27_tmp[j54] = fRec27_perm[j54];
			}
			for (int j56 = 0; j56 < 4; j56 = j56 + 1) {
				fRec26_tmp[j56] = fRec26_perm[j56];
			}
			for (int j58 = 0; j58 < 4; j58 = j58 + 1) {
				fRec25_tmp[j58] = fRec25_perm[j58];
			}
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				fRec29[i] = static_cast<float>(input5[i]) + fConst1 * fRec29[i - 1] - 2.8f * fRec25[i - 1];
				fRec28[i] = fRec29[i] + fConst1 * fRec28[i - 1];
				fRec27[i] = fRec28[i] + fConst1 * fRec27[i - 1];
				fRec26[i] = fRec27[i] + fConst1 * fRec26[i - 1];
				fRec25[i] = fConst2 * fRec26[i];
			}
			/* Post code */
			for (int j51 = 0; j51 < 4; j51 = j51 + 1) {
				fRec29_perm[j51] = fRec29_tmp[vsize + j51];
			}
			for (int j53 = 0; j53 < 4; j53 = j53 + 1) {
				fRec28_perm[j53] = fRec28_tmp[vsize + j53];
			}
			for (int j55 = 0; j55 < 4; j55 = j55 + 1) {
				fRec27_perm[j55] = fRec27_tmp[vsize + j55];
			}
			for (int j57 = 0; j57 < 4; j57 = j57 + 1) {
				fRec26_perm[j57] = fRec26_tmp[vsize + j57];
			}
			for (int j59 = 0; j59 < 4; j59 = j59 + 1) {
				fRec25_perm[j59] = fRec25_tmp[vsize + j59];
			}
			/* Recursive loop 6 */
			/* Pre code */
			for (int j60 = 0; j60 < 4; j60 = j60 + 1) {
				fRec34_tmp[j60] = fRec34_perm[j60];
			}
			for (int j62 = 0; j62 < 4; j62 = j62 + 1) {
				fRec33_tmp[j62] = fRec33_perm[j62];
			}
			for (int j64 = 0; j64 < 4; j64 = j64 + 1) {
				fRec32_tmp[j64] = fRec32_perm[j64];
			}
			for (int j66 = 0; j66 < 4; j66 = j66 + 1) {
				fRec31_tmp[j66] = fRec31_perm[j66];
			}
			for (int j68 = 0; j68 < 4; j68 = j68 + 1) {
				fRec30_tmp[j68] = fRec30_perm[j68];
			}
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				fRec34[i] = static_cast<float>(input6[i]) + fConst1 * fRec34[i - 1] - 2.8f * fRec30[i - 1];
				fRec33[i] = fRec34[i] + fConst1 * fRec33[i - 1];
				fRec32[i] = fRec33[i] + fConst1 * fRec32[i - 1];
				fRec31[i] = fRec32[i] + fConst1 * fRec31[i - 1];
				fRec30[i] = fConst2 * fRec31[i];
			}
			/* Post code */
			for (int j61 = 0; j61 < 4; j61 = j61 + 1) {
				fRec34_perm[j61] = fRec34_tmp[vsize + j61];
			}
			for (int j63 = 0; j63 < 4; j63 = j63 + 1) {
				fRec33_perm[j63] = fRec33_tmp[vsize + j63];
			}
			for (int j65 = 0; j65 < 4; j65 = j65 + 1) {
				fRec32_perm[j65] = fRec32_tmp[vsize + j65];
			}
			for (int j67 = 0; j67 < 4; j67 = j67 + 1) {
				fRec31_perm[j67] = fRec31_tmp[vsize + j67];
			}
			for (int j69 = 0; j69 < 4; j69 = j69 + 1) {
				fRec30_perm[j69] = fRec30_tmp[vsize + j69];
			}
			/* Recursive loop 7 */
			/* Pre code */
			for (int j70 = 0; j70 < 4; j70 = j70 + 1) {
				fRec39_tmp[j70] = fRec39_perm[j70];
			}
			for (int j72 = 0; j72 < 4; j72 = j72 + 1) {
				fRec38_tmp[j72] = fRec38_perm[j72];
			}
			for (int j74 = 0; j74 < 4; j74 = j74 + 1) {
				fRec37_tmp[j74] = fRec37_perm[j74];
			}
			for (int j76 = 0; j76 < 4; j76 = j76 + 1) {
				fRec36_tmp[j76] = fRec36_perm[j76];
			}
			for (int j78 = 0; j78 < 4; j78 = j78 + 1) {
				fRec35_tmp[j78] = fRec35_perm[j78];
			}
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				fRec39[i] = static_cast<float>(input7[i]) + fConst1 * fRec39[i - 1] - 2.8f * fRec35[i - 1];
				fRec38[i] = fRec39[i] + fConst1 * fRec38[i - 1];
				fRec37[i] = fRec38[i] + fConst1 * fRec37[i - 1];
				fRec36[i] = fRec37[i] + fConst1 * fRec36[i - 1];
				fRec35[i] = fConst2 * fRec36[i];
			}
			/* Post code */
			for (int j71 = 0; j71 < 4; j71 = j71 + 1) {
				fRec39_perm[j71] = fRec39_tmp[vsize + j71];
			}
			for (int j73 = 0; j73 < 4; j73 = j73 + 1) {
				fRec38_perm[j73] = fRec38_tmp[vsize + j73];
			}
			for (int j75 = 0; j75 < 4; j75 = j75 + 1) {
				fRec37_perm[j75] = fRec37_tmp[vsize + j75];
			}
			for (int j77 = 0; j77 < 4; j77 = j77 + 1) {
				fRec36_perm[j77] = fRec36_tmp[vsize + j77];
			}
			for (int j79 = 0; j79 < 4; j79 = j79 + 1) {
				fRec35_perm[j79] = fRec35_tmp[vsize + j79];
			}
			/* Recursive loop 8 */
			/* Pre code */
			for (int j80 = 0; j80 < 4; j80 = j80 + 1) {
				fRec44_tmp[j80] = fRec44_perm[j80];
			}
			for (int j82 = 0; j82 < 4; j82 = j82 + 1) {
				fRec43_tmp[j82] = fRec43_perm[j82];
			}
			for (int j84 = 0; j84 < 4; j84 = j84 + 1) {
				fRec42_tmp[j84] = fRec42_perm[j84];
			}
			for (int j86 = 0; j86 < 4; j86 = j86 + 1) {
				fRec41_tmp[j86] = fRec41_perm[j86];
			}
			for (int j88 = 0; j88 < 4; j88 = j88 + 1) {
				fRec40_tmp[j88] = fRec40_perm[j88];
			}
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				fRec44[i] = static_cast<float>(input8[i]) + fConst1 * fRec44[i - 1] - 2.8f * fRec40[i - 1];
				fRec43[i] = fRec44[i] + fConst1 * fRec43[i - 1];
				fRec42[i] = fRec43[i] + fConst1 * fRec42[i - 1];
				fRec41[i] = fRec42[i] + fConst1 * fRec41[i - 1];
				fRec40[i] = fConst2 * fRec41[i];
			}
			/* Post code */
			for (int j81 = 0; j81 < 4; j81 = j81 + 1) {
				fRec44_perm[j81] = fRec44_tmp[vsize + j81];
			}
			for (int j83 = 0; j83 < 4; j83 = j83 + 1) {
				fRec43_perm[j83] = fRec43_tmp[vsize + j83];
			}
			for (int j85 = 0; j85 < 4; j85 = j85 + 1) {
				fRec42_perm[j85] = fRec42_tmp[vsize + j85];
			}
			for (int j87 = 0; j87 < 4; j87 = j87 + 1) {
				fRec41_perm[j87] = fRec41_tmp[vsize + j87];
			}
			for (int j89 = 0; j89 < 4; j89 = j89 + 1) {
				fRec40_perm[j89] = fRec40_tmp[vsize + j89];
			}
			/* Recursive loop 9 */
			/* Pre code */
			for (int j90 = 0; j90 < 4; j90 = j90 + 1) {
				fRec49_tmp[j90] = fRec49_perm[j90];
			}
			for (int j92 = 0; j92 < 4; j92 = j92 + 1) {
				fRec48_tmp[j92] = fRec48_perm[j92];
			}
			for (int j94 = 0; j94 < 4; j94 = j94 + 1) {
				fRec47_tmp[j94] = fRec47_perm[j94];
			}
			for (int j96 = 0; j96 < 4; j96 = j96 + 1) {
				fRec46_tmp[j96] = fRec46_perm[j96];
			}
			for (int j98 = 0; j98 < 4; j98 = j98 + 1) {
				fRec45_tmp[j98] = fRec45_perm[j98];
			}
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				fRec49[i] = static_cast<float>(input9[i]) + fConst1 * fRec49[i - 1] - 2.8f * fRec45[i - 1];
				fRec48[i] = fRec49[i] + fConst1 * fRec48[i - 1];
				fRec47[i] = fRec48[i] + fConst1 * fRec47[i - 1];
				fRec46[i] = fRec47[i] + fConst1 * fRec46[i - 1];
				fRec45[i] = fConst2 * fRec46[i];
			}
			/* Post code */
			for (int j91 = 0; j91 < 4; j91 = j91 + 1) {
				fRec49_perm[j91] = fRec49_tmp[vsize + j91];
			}
			for (int j93 = 0; j93 < 4; j93 = j93 + 1) {
				fRec48_perm[j93] = fRec48_tmp[vsize + j93];
			}
			for (int j95 = 0; j95 < 4; j95 = j95 + 1) {
				fRec47_perm[j95] = fRec47_tmp[vsize + j95];
			}
			for (int j97 = 0; j97 < 4; j97 = j97 + 1) {
				fRec46_perm[j97] = fRec46_tmp[vsize + j97];
			}
			for (int j99 = 0; j99 < 4; j99 = j99 + 1) {
				fRec45_perm[j99] = fRec45_tmp[vsize + j99];
			}
			/* Recursive loop 10 */
			/* Pre code */
			for (int j100 = 0; j100 < 4; j100 = j100 + 1) {
				fRec54_tmp[j100] = fRec54_perm[j100];
			}
			for (int j102 = 0; j102 < 4; j102 = j102 + 1) {
				fRec53_tmp[j102] = fRec53_perm[j102];
			}
			for (int j104 = 0; j104 < 4; j104 = j104 + 1) {
				fRec52_tmp[j104] = fRec52_perm[j104];
			}
			for (int j106 = 0; j106 < 4; j106 = j106 + 1) {
				fRec51_tmp[j106] = fRec51_perm[j106];
			}
			for (int j108 = 0; j108 < 4; j108 = j108 + 1) {
				fRec50_tmp[j108] = fRec50_perm[j108];
			}
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				fRec54[i] = static_cast<float>(input10[i]) + fConst1 * fRec54[i - 1] - 2.8f * fRec50[i - 1];
				fRec53[i] = fRec54[i] + fConst1 * fRec53[i - 1];
				fRec52[i] = fRec53[i] + fConst1 * fRec52[i - 1];
				fRec51[i] = fRec52[i] + fConst1 * fRec51[i - 1];
				fRec50[i] = fConst2 * fRec51[i];
			}
			/* Post code */
			for (int j101 = 0; j101 < 4; j101 = j101 + 1) {
				fRec54_perm[j101] = fRec54_tmp[vsize + j101];
			}
			for (int j103 = 0; j103 < 4; j103 = j103 + 1) {
				fRec53_perm[j103] = fRec53_tmp[vsize + j103];
			}
			for (int j105 = 0; j105 < 4; j105 = j105 + 1) {
				fRec52_perm[j105] = fRec52_tmp[vsize + j105];
			}
			for (int j107 = 0; j107 < 4; j107 = j107 + 1) {
				fRec51_perm[j107] = fRec51_tmp[vsize + j107];
			}
			for (int j109 = 0; j109 < 4; j109 = j109 + 1) {
				fRec50_perm[j109] = fRec50_tmp[vsize + j109];
			}
			/* Recursive loop 11 */
			/* Pre code */
			for (int j110 = 0; j110 < 4; j110 = j110 + 1) {
				fRec59_tmp[j110] = fRec59_perm[j110];
			}
			for (int j112 = 0; j112 < 4; j112 = j112 + 1) {
				fRec58_tmp[j112] = fRec58_perm[j112];
			}
			for (int j114 = 0; j114 < 4; j114 = j114 + 1) {
				fRec57_tmp[j114] = fRec57_perm[j114];
			}
			for (int j116 = 0; j116 < 4; j116 = j116 + 1) {
				fRec56_tmp[j116] = fRec56_perm[j116];
			}
			for (int j118 = 0; j118 < 4; j118 = j118 + 1) {
				fRec55_tmp[j118] = fRec55_perm[j118];
			}
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				fRec59[i] = static_cast<float>(input11[i]) + fConst1 * fRec59[i - 1] - 2.8f * fRec55[i - 1];
				fRec58[i] = fRec59[i] + fConst1 * fRec58[i - 1];
				fRec57[i] = fRec58[i] + fConst1 * fRec57[i - 1];
				fRec56[i] = fRec57[i] + fConst1 * fRec56[i - 1];
				fRec55[i] = fConst2 * fRec56[i];
			}
			/* Post code */
			for (int j111 = 0; j111 < 4; j111 = j111 + 1) {
				fRec59_perm[j111] = fRec59_tmp[vsize + j111];
			}
			for (int j113 = 0; j113 < 4; j113 = j113 + 1) {
				fRec58_perm[j113] = fRec58_tmp[vsize + j113];
			}
			for (int j115 = 0; j115 < 4; j115 = j115 + 1) {
				fRec57_perm[j115] = fRec57_tmp[vsize + j115];
			}
			for (int j117 = 0; j117 < 4; j117 = j117 + 1) {
				fRec56_perm[j117] = fRec56_tmp[vsize + j117];
			}
			for (int j119 = 0; j119 < 4; j119 = j119 + 1) {
				fRec55_perm[j119] = fRec55_tmp[vsize + j119];
			}
			/* Recursive loop 12 */
			/* Pre code */
			for (int j120 = 0; j120 < 4; j120 = j120 + 1) {
				fRec64_tmp[j120] = fRec64_perm[j120];
			}
			for (int j122 = 0; j122 < 4; j122 = j122 + 1) {
				fRec63_tmp[j122] = fRec63_perm[j122];
			}
			for (int j124 = 0; j124 < 4; j124 = j124 + 1) {
				fRec62_tmp[j124] = fRec62_perm[j124];
			}
			for (int j126 = 0; j126 < 4; j126 = j126 + 1) {
				fRec61_tmp[j126] = fRec61_perm[j126];
			}
			for (int j128 = 0; j128 < 4; j128 = j128 + 1) {
				fRec60_tmp[j128] = fRec60_perm[j128];
			}
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				fRec64[i] = static_cast<float>(input12[i]) + fConst1 * fRec64[i - 1] - 2.8f * fRec60[i - 1];
				fRec63[i] = fRec64[i] + fConst1 * fRec63[i - 1];
				fRec62[i] = fRec63[i] + fConst1 * fRec62[i - 1];
				fRec61[i] = fRec62[i] + fConst1 * fRec61[i - 1];
				fRec60[i] = fConst2 * fRec61[i];
			}
			/* Post code */
			for (int j121 = 0; j121 < 4; j121 = j121 + 1) {
				fRec64_perm[j121] = fRec64_tmp[vsize + j121];
			}
			for (int j123 = 0; j123 < 4; j123 = j123 + 1) {
				fRec63_perm[j123] = fRec63_tmp[vsize + j123];
			}
			for (int j125 = 0; j125 < 4; j125 = j125 + 1) {
				fRec62_perm[j125] = fRec62_tmp[vsize + j125];
			}
			for (int j127 = 0; j127 < 4; j127 = j127 + 1) {
				fRec61_perm[j127] = fRec61_tmp[vsize + j127];
			}
			for (int j129 = 0; j129 < 4; j129 = j129 + 1) {
				fRec60_perm[j129] = fRec60_tmp[vsize + j129];
			}
			/* Recursive loop 13 */
			/* Pre code */
			for (int j130 = 0; j130 < 4; j130 = j130 + 1) {
				fRec69_tmp[j130] = fRec69_perm[j130];
			}
			for (int j132 = 0; j132 < 4; j132 = j132 + 1) {
				fRec68_tmp[j132] = fRec68_perm[j132];
			}
			for (int j134 = 0; j134 < 4; j134 = j134 + 1) {
				fRec67_tmp[j134] = fRec67_perm[j134];
			}
			for (int j136 = 0; j136 < 4; j136 = j136 + 1) {
				fRec66_tmp[j136] = fRec66_perm[j136];
			}
			for (int j138 = 0; j138 < 4; j138 = j138 + 1) {
				fRec65_tmp[j138] = fRec65_perm[j138];
			}
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				fRec69[i] = static_cast<float>(input13[i]) + fConst1 * fRec69[i - 1] - 2.8f * fRec65[i - 1];
				fRec68[i] = fRec69[i] + fConst1 * fRec68[i - 1];
				fRec67[i] = fRec68[i] + fConst1 * fRec67[i - 1];
				fRec66[i] = fRec67[i] + fConst1 * fRec66[i - 1];
				fRec65[i] = fConst2 * fRec66[i];
			}
			/* Post code */
			for (int j131 = 0; j131 < 4; j131 = j131 + 1) {
				fRec69_perm[j131] = fRec69_tmp[vsize + j131];
			}
			for (int j133 = 0; j133 < 4; j133 = j133 + 1) {
				fRec68_perm[j133] = fRec68_tmp[vsize + j133];
			}
			for (int j135 = 0; j135 < 4; j135 = j135 + 1) {
				fRec67_perm[j135] = fRec67_tmp[vsize + j135];
			}
			for (int j137 = 0; j137 < 4; j137 = j137 + 1) {
				fRec66_perm[j137] = fRec66_tmp[vsize + j137];
			}
			for (int j139 = 0; j139 < 4; j139 = j139 + 1) {
				fRec65_perm[j139] = fRec65_tmp[vsize + j139];
			}
			/* Recursive loop 14 */
			/* Pre code */
			for (int j140 = 0; j140 < 4; j140 = j140 + 1) {
				fRec74_tmp[j140] = fRec74_perm[j140];
			}
			for (int j142 = 0; j142 < 4; j142 = j142 + 1) {
				fRec73_tmp[j142] = fRec73_perm[j142];
			}
			for (int j144 = 0; j144 < 4; j144 = j144 + 1) {
				fRec72_tmp[j144] = fRec72_perm[j144];
			}
			for (int j146 = 0; j146 < 4; j146 = j146 + 1) {
				fRec71_tmp[j146] = fRec71_perm[j146];
			}
			for (int j148 = 0; j148 < 4; j148 = j148 + 1) {
				fRec70_tmp[j148] = fRec70_perm[j148];
			}
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				fRec74[i] = static_cast<float>(input14[i]) + fConst1 * fRec74[i - 1] - 2.8f * fRec70[i - 1];
				fRec73[i] = fRec74[i] + fConst1 * fRec73[i - 1];
				fRec72[i] = fRec73[i] + fConst1 * fRec72[i - 1];
				fRec71[i] = fRec72[i] + fConst1 * fRec71[i - 1];
				fRec70[i] = fConst2 * fRec71[i];
			}
			/* Post code */
			for (int j141 = 0; j141 < 4; j141 = j141 + 1) {
				fRec74_perm[j141] = fRec74_tmp[vsize + j141];
			}
			for (int j143 = 0; j143 < 4; j143 = j143 + 1) {
				fRec73_perm[j143] = fRec73_tmp[vsize + j143];
			}
			for (int j145 = 0; j145 < 4; j145 = j145 + 1) {
				fRec72_perm[j145] = fRec72_tmp[vsize + j145];
			}
			for (int j147 = 0; j147 < 4; j147 = j147 + 1) {
				fRec71_perm[j147] = fRec71_tmp[vsize + j147];
			}
			for (int j149 = 0; j149 < 4; j149 = j149 + 1) {
				fRec70_perm[j149] = fRec70_tmp[vsize + j149];
			}
			/* Recursive loop 15 */
			/* Pre code */
			for (int j150 = 0; j150 < 4; j150 = j150 + 1) {
				fRec79_tmp[j150] = fRec79_perm[j150];
			}
			for (int j152 = 0; j152 < 4; j152 = j152 + 1) {
				fRec78_tmp[j152] = fRec78_perm[j152];
			}
			for (int j154 = 0; j154 < 4; j154 = j154 + 1) {
				fRec77_tmp[j154] = fRec77_perm[j154];
			}
			for (int j156 = 0; j156 < 4; j156 = j156 + 1) {
				fRec76_tmp[j156] = fRec76_perm[j156];
			}
			for (int j158 = 0; j158 < 4; j158 = j158 + 1) {
				fRec75_tmp[j158] = fRec75_perm[j158];
			}
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				fRec79[i] = static_cast<float>(input15[i]) + fConst1 * fRec79[i - 1] - 2.8f * fRec75[i - 1];
				fRec78[i] = fRec79[i] + fConst1 * fRec78[i - 1];
				fRec77[i] = fRec78[i] + fConst1 * fRec77[i - 1];
				fRec76[i] = fRec77[i] + fConst1 * fRec76[i - 1];
				fRec75[i] = fConst2 * fRec76[i];
			}
			/* Post code */
			for (int j151 = 0; j151 < 4; j151 = j151 + 1) {
				fRec79_perm[j151] = fRec79_tmp[vsize + j151];
			}
			for (int j153 = 0; j153 < 4; j153 = j153 + 1) {
				fRec78_perm[j153] = fRec78_tmp[vsize + j153];
			}
			for (int j155 = 0; j155 < 4; j155 = j155 + 1) {
				fRec77_perm[j155] = fRec77_tmp[vsize + j155];
			}
			for (int j157 = 0; j157 < 4; j157 = j157 + 1) {
				fRec76_perm[j157] = fRec76_tmp[vsize + j157];
			}
			for (int j159 = 0; j159 < 4; j159 = j159 + 1) {
				fRec75_perm[j159] = fRec75_tmp[vsize + j159];
			}
			/* Vectorizable loop 16 */
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				output0[i] = static_cast<FAUSTFLOAT>(fRec0[i]);
			}
			/* Vectorizable loop 17 */
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				output1[i] = static_cast<FAUSTFLOAT>(fRec5[i]);
			}
			/* Vectorizable loop 18 */
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				output2[i] = static_cast<FAUSTFLOAT>(fRec10[i]);
			}
			/* Vectorizable loop 19 */
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				output3[i] = static_cast<FAUSTFLOAT>(fRec15[i]);
			}
			/* Vectorizable loop 20 */
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				output4[i] = static_cast<FAUSTFLOAT>(fRec20[i]);
			}
			/* Vectorizable loop 21 */
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				output5[i] = static_cast<FAUSTFLOAT>(fRec25[i]);
			}
			/* Vectorizable loop 22 */
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				output6[i] = static_cast<FAUSTFLOAT>(fRec30[i]);
			}
			/* Vectorizable loop 23 */
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				output7[i] = static_cast<FAUSTFLOAT>(fRec35[i]);
			}
			/* Vectorizable loop 24 */
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				output8[i] = static_cast<FAUSTFLOAT>(fRec40[i]);
			}
			/* Vectorizable loop 25 */
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				output9[i] = static_cast<FAUSTFLOAT>(fRec45[i]);
			}
			/* Vectorizable loop 26 */
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				output10[i] = static_cast<FAUSTFLOAT>(fRec50[i]);
			}
			/* Vectorizable loop 27 */
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				output11[i] = static_cast<FAUSTFLOAT>(fRec55[i]);
			}
			/* Vectorizable loop 28 */
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				output12[i] = static_cast<FAUSTFLOAT>(fRec60[i]);
			}
			/* Vectorizable loop 29 */
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				output13[i] = static_cast<FAUSTFLOAT>(fRec65[i]);
			}
			/* Vectorizable loop 30 */
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				output14[i] = static_cast<FAUSTFLOAT>(fRec70[i]);
			}
			/* Vectorizable loop 31 */
			/* Compute code */
			for (int i = 0; i < vsize; i = i + 1) {
				output15[i] = static_cast<FAUSTFLOAT>(fRec75[i]);
			}
		}
	}

};

#endif

// --- heinzel factory (appended by tools/gen-faust.sh) ---
dsp* heinzel_make_ladder16_vec() { return new heinzel_ladder16_vec(); }
