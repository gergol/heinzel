/* ------------------------------------------------------------
name: "ladder16"
Code generated with Faust 2.85.5 (https://faust.grame.fr)
Compilation options: -lang cpp -fpga-mem-th 4 -light -ct 1 -cn heinzel_ladder16_scal -es 1 -mcd 16 -mdd 1024 -mdy 33 -single -ftz 0
------------------------------------------------------------ */

#ifndef  __heinzel_ladder16_scal_H__
#define  __heinzel_ladder16_scal_H__

#ifndef FAUSTFLOAT
#define FAUSTFLOAT float
#endif 

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <math.h>

#ifndef FAUSTCLASS 
#define FAUSTCLASS heinzel_ladder16_scal
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

static float heinzel_ladder16_scal_faustpower4_f(float value) {
	return value * value * value * value;
}

class heinzel_ladder16_scal : public dsp {
	
 private:
	
	int fSampleRate;
	float fConst0;
	float fConst1;
	float fRec4[2];
	float fRec3[2];
	float fRec2[2];
	float fRec1[2];
	float fConst2;
	float fRec0[2];
	float fRec9[2];
	float fRec8[2];
	float fRec7[2];
	float fRec6[2];
	float fRec5[2];
	float fRec14[2];
	float fRec13[2];
	float fRec12[2];
	float fRec11[2];
	float fRec10[2];
	float fRec19[2];
	float fRec18[2];
	float fRec17[2];
	float fRec16[2];
	float fRec15[2];
	float fRec24[2];
	float fRec23[2];
	float fRec22[2];
	float fRec21[2];
	float fRec20[2];
	float fRec29[2];
	float fRec28[2];
	float fRec27[2];
	float fRec26[2];
	float fRec25[2];
	float fRec34[2];
	float fRec33[2];
	float fRec32[2];
	float fRec31[2];
	float fRec30[2];
	float fRec39[2];
	float fRec38[2];
	float fRec37[2];
	float fRec36[2];
	float fRec35[2];
	float fRec44[2];
	float fRec43[2];
	float fRec42[2];
	float fRec41[2];
	float fRec40[2];
	float fRec49[2];
	float fRec48[2];
	float fRec47[2];
	float fRec46[2];
	float fRec45[2];
	float fRec54[2];
	float fRec53[2];
	float fRec52[2];
	float fRec51[2];
	float fRec50[2];
	float fRec59[2];
	float fRec58[2];
	float fRec57[2];
	float fRec56[2];
	float fRec55[2];
	float fRec64[2];
	float fRec63[2];
	float fRec62[2];
	float fRec61[2];
	float fRec60[2];
	float fRec69[2];
	float fRec68[2];
	float fRec67[2];
	float fRec66[2];
	float fRec65[2];
	float fRec74[2];
	float fRec73[2];
	float fRec72[2];
	float fRec71[2];
	float fRec70[2];
	float fRec79[2];
	float fRec78[2];
	float fRec77[2];
	float fRec76[2];
	float fRec75[2];
	
 public:
	heinzel_ladder16_scal() {
	}
	
	heinzel_ladder16_scal(const heinzel_ladder16_scal&) = default;
	
	virtual ~heinzel_ladder16_scal() = default;
	
	heinzel_ladder16_scal& operator=(const heinzel_ladder16_scal&) = default;
	
	void metadata(Meta* m) { 
		m->declare("compile_options", "-lang cpp -fpga-mem-th 4 -light -ct 1 -cn heinzel_ladder16_scal -es 1 -mcd 16 -mdd 1024 -mdy 33 -single -ftz 0");
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
		fConst2 = heinzel_ladder16_scal_faustpower4_f(fConst0);
	}
	
	virtual void instanceResetUserInterface() {
	}
	
	virtual void instanceClear() {
		for (int l0 = 0; l0 < 2; l0 = l0 + 1) {
			fRec4[l0] = 0.0f;
		}
		for (int l1 = 0; l1 < 2; l1 = l1 + 1) {
			fRec3[l1] = 0.0f;
		}
		for (int l2 = 0; l2 < 2; l2 = l2 + 1) {
			fRec2[l2] = 0.0f;
		}
		for (int l3 = 0; l3 < 2; l3 = l3 + 1) {
			fRec1[l3] = 0.0f;
		}
		for (int l4 = 0; l4 < 2; l4 = l4 + 1) {
			fRec0[l4] = 0.0f;
		}
		for (int l5 = 0; l5 < 2; l5 = l5 + 1) {
			fRec9[l5] = 0.0f;
		}
		for (int l6 = 0; l6 < 2; l6 = l6 + 1) {
			fRec8[l6] = 0.0f;
		}
		for (int l7 = 0; l7 < 2; l7 = l7 + 1) {
			fRec7[l7] = 0.0f;
		}
		for (int l8 = 0; l8 < 2; l8 = l8 + 1) {
			fRec6[l8] = 0.0f;
		}
		for (int l9 = 0; l9 < 2; l9 = l9 + 1) {
			fRec5[l9] = 0.0f;
		}
		for (int l10 = 0; l10 < 2; l10 = l10 + 1) {
			fRec14[l10] = 0.0f;
		}
		for (int l11 = 0; l11 < 2; l11 = l11 + 1) {
			fRec13[l11] = 0.0f;
		}
		for (int l12 = 0; l12 < 2; l12 = l12 + 1) {
			fRec12[l12] = 0.0f;
		}
		for (int l13 = 0; l13 < 2; l13 = l13 + 1) {
			fRec11[l13] = 0.0f;
		}
		for (int l14 = 0; l14 < 2; l14 = l14 + 1) {
			fRec10[l14] = 0.0f;
		}
		for (int l15 = 0; l15 < 2; l15 = l15 + 1) {
			fRec19[l15] = 0.0f;
		}
		for (int l16 = 0; l16 < 2; l16 = l16 + 1) {
			fRec18[l16] = 0.0f;
		}
		for (int l17 = 0; l17 < 2; l17 = l17 + 1) {
			fRec17[l17] = 0.0f;
		}
		for (int l18 = 0; l18 < 2; l18 = l18 + 1) {
			fRec16[l18] = 0.0f;
		}
		for (int l19 = 0; l19 < 2; l19 = l19 + 1) {
			fRec15[l19] = 0.0f;
		}
		for (int l20 = 0; l20 < 2; l20 = l20 + 1) {
			fRec24[l20] = 0.0f;
		}
		for (int l21 = 0; l21 < 2; l21 = l21 + 1) {
			fRec23[l21] = 0.0f;
		}
		for (int l22 = 0; l22 < 2; l22 = l22 + 1) {
			fRec22[l22] = 0.0f;
		}
		for (int l23 = 0; l23 < 2; l23 = l23 + 1) {
			fRec21[l23] = 0.0f;
		}
		for (int l24 = 0; l24 < 2; l24 = l24 + 1) {
			fRec20[l24] = 0.0f;
		}
		for (int l25 = 0; l25 < 2; l25 = l25 + 1) {
			fRec29[l25] = 0.0f;
		}
		for (int l26 = 0; l26 < 2; l26 = l26 + 1) {
			fRec28[l26] = 0.0f;
		}
		for (int l27 = 0; l27 < 2; l27 = l27 + 1) {
			fRec27[l27] = 0.0f;
		}
		for (int l28 = 0; l28 < 2; l28 = l28 + 1) {
			fRec26[l28] = 0.0f;
		}
		for (int l29 = 0; l29 < 2; l29 = l29 + 1) {
			fRec25[l29] = 0.0f;
		}
		for (int l30 = 0; l30 < 2; l30 = l30 + 1) {
			fRec34[l30] = 0.0f;
		}
		for (int l31 = 0; l31 < 2; l31 = l31 + 1) {
			fRec33[l31] = 0.0f;
		}
		for (int l32 = 0; l32 < 2; l32 = l32 + 1) {
			fRec32[l32] = 0.0f;
		}
		for (int l33 = 0; l33 < 2; l33 = l33 + 1) {
			fRec31[l33] = 0.0f;
		}
		for (int l34 = 0; l34 < 2; l34 = l34 + 1) {
			fRec30[l34] = 0.0f;
		}
		for (int l35 = 0; l35 < 2; l35 = l35 + 1) {
			fRec39[l35] = 0.0f;
		}
		for (int l36 = 0; l36 < 2; l36 = l36 + 1) {
			fRec38[l36] = 0.0f;
		}
		for (int l37 = 0; l37 < 2; l37 = l37 + 1) {
			fRec37[l37] = 0.0f;
		}
		for (int l38 = 0; l38 < 2; l38 = l38 + 1) {
			fRec36[l38] = 0.0f;
		}
		for (int l39 = 0; l39 < 2; l39 = l39 + 1) {
			fRec35[l39] = 0.0f;
		}
		for (int l40 = 0; l40 < 2; l40 = l40 + 1) {
			fRec44[l40] = 0.0f;
		}
		for (int l41 = 0; l41 < 2; l41 = l41 + 1) {
			fRec43[l41] = 0.0f;
		}
		for (int l42 = 0; l42 < 2; l42 = l42 + 1) {
			fRec42[l42] = 0.0f;
		}
		for (int l43 = 0; l43 < 2; l43 = l43 + 1) {
			fRec41[l43] = 0.0f;
		}
		for (int l44 = 0; l44 < 2; l44 = l44 + 1) {
			fRec40[l44] = 0.0f;
		}
		for (int l45 = 0; l45 < 2; l45 = l45 + 1) {
			fRec49[l45] = 0.0f;
		}
		for (int l46 = 0; l46 < 2; l46 = l46 + 1) {
			fRec48[l46] = 0.0f;
		}
		for (int l47 = 0; l47 < 2; l47 = l47 + 1) {
			fRec47[l47] = 0.0f;
		}
		for (int l48 = 0; l48 < 2; l48 = l48 + 1) {
			fRec46[l48] = 0.0f;
		}
		for (int l49 = 0; l49 < 2; l49 = l49 + 1) {
			fRec45[l49] = 0.0f;
		}
		for (int l50 = 0; l50 < 2; l50 = l50 + 1) {
			fRec54[l50] = 0.0f;
		}
		for (int l51 = 0; l51 < 2; l51 = l51 + 1) {
			fRec53[l51] = 0.0f;
		}
		for (int l52 = 0; l52 < 2; l52 = l52 + 1) {
			fRec52[l52] = 0.0f;
		}
		for (int l53 = 0; l53 < 2; l53 = l53 + 1) {
			fRec51[l53] = 0.0f;
		}
		for (int l54 = 0; l54 < 2; l54 = l54 + 1) {
			fRec50[l54] = 0.0f;
		}
		for (int l55 = 0; l55 < 2; l55 = l55 + 1) {
			fRec59[l55] = 0.0f;
		}
		for (int l56 = 0; l56 < 2; l56 = l56 + 1) {
			fRec58[l56] = 0.0f;
		}
		for (int l57 = 0; l57 < 2; l57 = l57 + 1) {
			fRec57[l57] = 0.0f;
		}
		for (int l58 = 0; l58 < 2; l58 = l58 + 1) {
			fRec56[l58] = 0.0f;
		}
		for (int l59 = 0; l59 < 2; l59 = l59 + 1) {
			fRec55[l59] = 0.0f;
		}
		for (int l60 = 0; l60 < 2; l60 = l60 + 1) {
			fRec64[l60] = 0.0f;
		}
		for (int l61 = 0; l61 < 2; l61 = l61 + 1) {
			fRec63[l61] = 0.0f;
		}
		for (int l62 = 0; l62 < 2; l62 = l62 + 1) {
			fRec62[l62] = 0.0f;
		}
		for (int l63 = 0; l63 < 2; l63 = l63 + 1) {
			fRec61[l63] = 0.0f;
		}
		for (int l64 = 0; l64 < 2; l64 = l64 + 1) {
			fRec60[l64] = 0.0f;
		}
		for (int l65 = 0; l65 < 2; l65 = l65 + 1) {
			fRec69[l65] = 0.0f;
		}
		for (int l66 = 0; l66 < 2; l66 = l66 + 1) {
			fRec68[l66] = 0.0f;
		}
		for (int l67 = 0; l67 < 2; l67 = l67 + 1) {
			fRec67[l67] = 0.0f;
		}
		for (int l68 = 0; l68 < 2; l68 = l68 + 1) {
			fRec66[l68] = 0.0f;
		}
		for (int l69 = 0; l69 < 2; l69 = l69 + 1) {
			fRec65[l69] = 0.0f;
		}
		for (int l70 = 0; l70 < 2; l70 = l70 + 1) {
			fRec74[l70] = 0.0f;
		}
		for (int l71 = 0; l71 < 2; l71 = l71 + 1) {
			fRec73[l71] = 0.0f;
		}
		for (int l72 = 0; l72 < 2; l72 = l72 + 1) {
			fRec72[l72] = 0.0f;
		}
		for (int l73 = 0; l73 < 2; l73 = l73 + 1) {
			fRec71[l73] = 0.0f;
		}
		for (int l74 = 0; l74 < 2; l74 = l74 + 1) {
			fRec70[l74] = 0.0f;
		}
		for (int l75 = 0; l75 < 2; l75 = l75 + 1) {
			fRec79[l75] = 0.0f;
		}
		for (int l76 = 0; l76 < 2; l76 = l76 + 1) {
			fRec78[l76] = 0.0f;
		}
		for (int l77 = 0; l77 < 2; l77 = l77 + 1) {
			fRec77[l77] = 0.0f;
		}
		for (int l78 = 0; l78 < 2; l78 = l78 + 1) {
			fRec76[l78] = 0.0f;
		}
		for (int l79 = 0; l79 < 2; l79 = l79 + 1) {
			fRec75[l79] = 0.0f;
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
	
	virtual heinzel_ladder16_scal* clone() {
		return new heinzel_ladder16_scal(*this);
	}
	
	virtual int getSampleRate() {
		return fSampleRate;
	}
	
	virtual void buildUserInterface(UI* ui_interface) {
		ui_interface->openVerticalBox("ladder16");
		ui_interface->closeBox();
	}
	
	virtual void compute(int count, FAUSTFLOAT** RESTRICT inputs, FAUSTFLOAT** RESTRICT outputs) {
		FAUSTFLOAT* input0 = inputs[0];
		FAUSTFLOAT* input1 = inputs[1];
		FAUSTFLOAT* input2 = inputs[2];
		FAUSTFLOAT* input3 = inputs[3];
		FAUSTFLOAT* input4 = inputs[4];
		FAUSTFLOAT* input5 = inputs[5];
		FAUSTFLOAT* input6 = inputs[6];
		FAUSTFLOAT* input7 = inputs[7];
		FAUSTFLOAT* input8 = inputs[8];
		FAUSTFLOAT* input9 = inputs[9];
		FAUSTFLOAT* input10 = inputs[10];
		FAUSTFLOAT* input11 = inputs[11];
		FAUSTFLOAT* input12 = inputs[12];
		FAUSTFLOAT* input13 = inputs[13];
		FAUSTFLOAT* input14 = inputs[14];
		FAUSTFLOAT* input15 = inputs[15];
		FAUSTFLOAT* output0 = outputs[0];
		FAUSTFLOAT* output1 = outputs[1];
		FAUSTFLOAT* output2 = outputs[2];
		FAUSTFLOAT* output3 = outputs[3];
		FAUSTFLOAT* output4 = outputs[4];
		FAUSTFLOAT* output5 = outputs[5];
		FAUSTFLOAT* output6 = outputs[6];
		FAUSTFLOAT* output7 = outputs[7];
		FAUSTFLOAT* output8 = outputs[8];
		FAUSTFLOAT* output9 = outputs[9];
		FAUSTFLOAT* output10 = outputs[10];
		FAUSTFLOAT* output11 = outputs[11];
		FAUSTFLOAT* output12 = outputs[12];
		FAUSTFLOAT* output13 = outputs[13];
		FAUSTFLOAT* output14 = outputs[14];
		FAUSTFLOAT* output15 = outputs[15];
		for (int i0 = 0; i0 < count; i0 = i0 + 1) {
			fRec4[0] = static_cast<float>(input0[i0]) + fConst1 * fRec4[1] - 2.8f * fRec0[1];
			fRec3[0] = fRec4[0] + fConst1 * fRec3[1];
			fRec2[0] = fRec3[0] + fConst1 * fRec2[1];
			fRec1[0] = fRec2[0] + fConst1 * fRec1[1];
			fRec0[0] = fConst2 * fRec1[0];
			output0[i0] = static_cast<FAUSTFLOAT>(fRec0[0]);
			fRec9[0] = static_cast<float>(input1[i0]) + fConst1 * fRec9[1] - 2.8f * fRec5[1];
			fRec8[0] = fRec9[0] + fConst1 * fRec8[1];
			fRec7[0] = fRec8[0] + fConst1 * fRec7[1];
			fRec6[0] = fRec7[0] + fConst1 * fRec6[1];
			fRec5[0] = fConst2 * fRec6[0];
			output1[i0] = static_cast<FAUSTFLOAT>(fRec5[0]);
			fRec14[0] = static_cast<float>(input2[i0]) + fConst1 * fRec14[1] - 2.8f * fRec10[1];
			fRec13[0] = fRec14[0] + fConst1 * fRec13[1];
			fRec12[0] = fRec13[0] + fConst1 * fRec12[1];
			fRec11[0] = fRec12[0] + fConst1 * fRec11[1];
			fRec10[0] = fConst2 * fRec11[0];
			output2[i0] = static_cast<FAUSTFLOAT>(fRec10[0]);
			fRec19[0] = static_cast<float>(input3[i0]) + fConst1 * fRec19[1] - 2.8f * fRec15[1];
			fRec18[0] = fRec19[0] + fConst1 * fRec18[1];
			fRec17[0] = fRec18[0] + fConst1 * fRec17[1];
			fRec16[0] = fRec17[0] + fConst1 * fRec16[1];
			fRec15[0] = fConst2 * fRec16[0];
			output3[i0] = static_cast<FAUSTFLOAT>(fRec15[0]);
			fRec24[0] = static_cast<float>(input4[i0]) + fConst1 * fRec24[1] - 2.8f * fRec20[1];
			fRec23[0] = fRec24[0] + fConst1 * fRec23[1];
			fRec22[0] = fRec23[0] + fConst1 * fRec22[1];
			fRec21[0] = fRec22[0] + fConst1 * fRec21[1];
			fRec20[0] = fConst2 * fRec21[0];
			output4[i0] = static_cast<FAUSTFLOAT>(fRec20[0]);
			fRec29[0] = static_cast<float>(input5[i0]) + fConst1 * fRec29[1] - 2.8f * fRec25[1];
			fRec28[0] = fRec29[0] + fConst1 * fRec28[1];
			fRec27[0] = fRec28[0] + fConst1 * fRec27[1];
			fRec26[0] = fRec27[0] + fConst1 * fRec26[1];
			fRec25[0] = fConst2 * fRec26[0];
			output5[i0] = static_cast<FAUSTFLOAT>(fRec25[0]);
			fRec34[0] = static_cast<float>(input6[i0]) + fConst1 * fRec34[1] - 2.8f * fRec30[1];
			fRec33[0] = fRec34[0] + fConst1 * fRec33[1];
			fRec32[0] = fRec33[0] + fConst1 * fRec32[1];
			fRec31[0] = fRec32[0] + fConst1 * fRec31[1];
			fRec30[0] = fConst2 * fRec31[0];
			output6[i0] = static_cast<FAUSTFLOAT>(fRec30[0]);
			fRec39[0] = static_cast<float>(input7[i0]) + fConst1 * fRec39[1] - 2.8f * fRec35[1];
			fRec38[0] = fRec39[0] + fConst1 * fRec38[1];
			fRec37[0] = fRec38[0] + fConst1 * fRec37[1];
			fRec36[0] = fRec37[0] + fConst1 * fRec36[1];
			fRec35[0] = fConst2 * fRec36[0];
			output7[i0] = static_cast<FAUSTFLOAT>(fRec35[0]);
			fRec44[0] = static_cast<float>(input8[i0]) + fConst1 * fRec44[1] - 2.8f * fRec40[1];
			fRec43[0] = fRec44[0] + fConst1 * fRec43[1];
			fRec42[0] = fRec43[0] + fConst1 * fRec42[1];
			fRec41[0] = fRec42[0] + fConst1 * fRec41[1];
			fRec40[0] = fConst2 * fRec41[0];
			output8[i0] = static_cast<FAUSTFLOAT>(fRec40[0]);
			fRec49[0] = static_cast<float>(input9[i0]) + fConst1 * fRec49[1] - 2.8f * fRec45[1];
			fRec48[0] = fRec49[0] + fConst1 * fRec48[1];
			fRec47[0] = fRec48[0] + fConst1 * fRec47[1];
			fRec46[0] = fRec47[0] + fConst1 * fRec46[1];
			fRec45[0] = fConst2 * fRec46[0];
			output9[i0] = static_cast<FAUSTFLOAT>(fRec45[0]);
			fRec54[0] = static_cast<float>(input10[i0]) + fConst1 * fRec54[1] - 2.8f * fRec50[1];
			fRec53[0] = fRec54[0] + fConst1 * fRec53[1];
			fRec52[0] = fRec53[0] + fConst1 * fRec52[1];
			fRec51[0] = fRec52[0] + fConst1 * fRec51[1];
			fRec50[0] = fConst2 * fRec51[0];
			output10[i0] = static_cast<FAUSTFLOAT>(fRec50[0]);
			fRec59[0] = static_cast<float>(input11[i0]) + fConst1 * fRec59[1] - 2.8f * fRec55[1];
			fRec58[0] = fRec59[0] + fConst1 * fRec58[1];
			fRec57[0] = fRec58[0] + fConst1 * fRec57[1];
			fRec56[0] = fRec57[0] + fConst1 * fRec56[1];
			fRec55[0] = fConst2 * fRec56[0];
			output11[i0] = static_cast<FAUSTFLOAT>(fRec55[0]);
			fRec64[0] = static_cast<float>(input12[i0]) + fConst1 * fRec64[1] - 2.8f * fRec60[1];
			fRec63[0] = fRec64[0] + fConst1 * fRec63[1];
			fRec62[0] = fRec63[0] + fConst1 * fRec62[1];
			fRec61[0] = fRec62[0] + fConst1 * fRec61[1];
			fRec60[0] = fConst2 * fRec61[0];
			output12[i0] = static_cast<FAUSTFLOAT>(fRec60[0]);
			fRec69[0] = static_cast<float>(input13[i0]) + fConst1 * fRec69[1] - 2.8f * fRec65[1];
			fRec68[0] = fRec69[0] + fConst1 * fRec68[1];
			fRec67[0] = fRec68[0] + fConst1 * fRec67[1];
			fRec66[0] = fRec67[0] + fConst1 * fRec66[1];
			fRec65[0] = fConst2 * fRec66[0];
			output13[i0] = static_cast<FAUSTFLOAT>(fRec65[0]);
			fRec74[0] = static_cast<float>(input14[i0]) + fConst1 * fRec74[1] - 2.8f * fRec70[1];
			fRec73[0] = fRec74[0] + fConst1 * fRec73[1];
			fRec72[0] = fRec73[0] + fConst1 * fRec72[1];
			fRec71[0] = fRec72[0] + fConst1 * fRec71[1];
			fRec70[0] = fConst2 * fRec71[0];
			output14[i0] = static_cast<FAUSTFLOAT>(fRec70[0]);
			fRec79[0] = static_cast<float>(input15[i0]) + fConst1 * fRec79[1] - 2.8f * fRec75[1];
			fRec78[0] = fRec79[0] + fConst1 * fRec78[1];
			fRec77[0] = fRec78[0] + fConst1 * fRec77[1];
			fRec76[0] = fRec77[0] + fConst1 * fRec76[1];
			fRec75[0] = fConst2 * fRec76[0];
			output15[i0] = static_cast<FAUSTFLOAT>(fRec75[0]);
			fRec4[1] = fRec4[0];
			fRec3[1] = fRec3[0];
			fRec2[1] = fRec2[0];
			fRec1[1] = fRec1[0];
			fRec0[1] = fRec0[0];
			fRec9[1] = fRec9[0];
			fRec8[1] = fRec8[0];
			fRec7[1] = fRec7[0];
			fRec6[1] = fRec6[0];
			fRec5[1] = fRec5[0];
			fRec14[1] = fRec14[0];
			fRec13[1] = fRec13[0];
			fRec12[1] = fRec12[0];
			fRec11[1] = fRec11[0];
			fRec10[1] = fRec10[0];
			fRec19[1] = fRec19[0];
			fRec18[1] = fRec18[0];
			fRec17[1] = fRec17[0];
			fRec16[1] = fRec16[0];
			fRec15[1] = fRec15[0];
			fRec24[1] = fRec24[0];
			fRec23[1] = fRec23[0];
			fRec22[1] = fRec22[0];
			fRec21[1] = fRec21[0];
			fRec20[1] = fRec20[0];
			fRec29[1] = fRec29[0];
			fRec28[1] = fRec28[0];
			fRec27[1] = fRec27[0];
			fRec26[1] = fRec26[0];
			fRec25[1] = fRec25[0];
			fRec34[1] = fRec34[0];
			fRec33[1] = fRec33[0];
			fRec32[1] = fRec32[0];
			fRec31[1] = fRec31[0];
			fRec30[1] = fRec30[0];
			fRec39[1] = fRec39[0];
			fRec38[1] = fRec38[0];
			fRec37[1] = fRec37[0];
			fRec36[1] = fRec36[0];
			fRec35[1] = fRec35[0];
			fRec44[1] = fRec44[0];
			fRec43[1] = fRec43[0];
			fRec42[1] = fRec42[0];
			fRec41[1] = fRec41[0];
			fRec40[1] = fRec40[0];
			fRec49[1] = fRec49[0];
			fRec48[1] = fRec48[0];
			fRec47[1] = fRec47[0];
			fRec46[1] = fRec46[0];
			fRec45[1] = fRec45[0];
			fRec54[1] = fRec54[0];
			fRec53[1] = fRec53[0];
			fRec52[1] = fRec52[0];
			fRec51[1] = fRec51[0];
			fRec50[1] = fRec50[0];
			fRec59[1] = fRec59[0];
			fRec58[1] = fRec58[0];
			fRec57[1] = fRec57[0];
			fRec56[1] = fRec56[0];
			fRec55[1] = fRec55[0];
			fRec64[1] = fRec64[0];
			fRec63[1] = fRec63[0];
			fRec62[1] = fRec62[0];
			fRec61[1] = fRec61[0];
			fRec60[1] = fRec60[0];
			fRec69[1] = fRec69[0];
			fRec68[1] = fRec68[0];
			fRec67[1] = fRec67[0];
			fRec66[1] = fRec66[0];
			fRec65[1] = fRec65[0];
			fRec74[1] = fRec74[0];
			fRec73[1] = fRec73[0];
			fRec72[1] = fRec72[0];
			fRec71[1] = fRec71[0];
			fRec70[1] = fRec70[0];
			fRec79[1] = fRec79[0];
			fRec78[1] = fRec78[0];
			fRec77[1] = fRec77[0];
			fRec76[1] = fRec76[0];
			fRec75[1] = fRec75[0];
		}
	}

};

#endif

// --- heinzel factory (appended by tools/gen-faust.sh) ---
dsp* heinzel_make_ladder16_scal() { return new heinzel_ladder16_scal(); }
