#include "Template.hpp"

#include <iostream>
#include <iomanip>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wswitch"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#include "deps/adlmidi/dbopl.h"
#pragma GCC diagnostic pop

static const unsigned int kChannels = 6;
static const unsigned int kOperatorsPerChannel = 4;

// Mapping to actual hardware channel A (because channels come in A/B
// pairs when using 4-op synthesis) for each of our virtual channels
// (1-6). http://www.shikadi.net/moddingwiki/OPL_chip#Register_Map
static const unsigned int kHWChannels[kChannels] = {0, 1, 2, 9, 10, 11};

// Mapping of virtual channel (0-6) to the first operator for that
// channel.  http://www.shikadi.net/moddingwiki/OPL_chip#Register_Map
static const unsigned int kHWOperatorForChannel[kChannels] = {0, 1, 2, 18, 19, 20};

struct OperatorConfigEffects {
  uint8_t tremolo : 1,
    vibrato : 1,
    sustain: 1,
    ksr: 1,
    multi: 4;

  uint8_t value() const {
    return (tremolo << 7) | (vibrato << 6) | (sustain << 5) | (ksr << 4) | multi;
  }
};
static_assert(sizeof(OperatorConfigEffects) == sizeof(uint8_t));

struct OperatorConfigLevels {
  uint8_t ksl: 2,
    level: 6;

  uint8_t value() const {
    return (ksl << 6) | level;
  }
};
static_assert(sizeof(OperatorConfigLevels) == sizeof(uint8_t));

struct OperatorConfigAtkDec {
  uint8_t attack: 4,
    decay: 4;

  uint8_t value() const {
    return (attack << 4) | decay;
  }
};
static_assert(sizeof(OperatorConfigAtkDec) == sizeof(uint8_t));

struct OperatorConfigSusRel {
  uint8_t sustain: 4,
    release: 4;

  uint8_t value() const {
    return (sustain << 4) | release;
  }
};
static_assert(sizeof(OperatorConfigSusRel) == sizeof(uint8_t));

struct OperatorConfigWaveform {
  uint8_t waveform: 3;

  uint8_t value() const {
    return waveform;
  }
};
static_assert(sizeof(OperatorConfigWaveform) == sizeof(uint8_t));

struct ChannelConfigNote {
  struct {
    uint8_t freqlow8bits: 8;

    uint8_t value() const {
      return freqlow8bits;
    }
  } A; // A0~A8 registers
  struct {
    uint8_t keyon: 1, block: 3, freqhi2bits: 2;

    uint8_t value() const {
      return (keyon << 5) | (block << 2) | freqhi2bits;
    }
  } B; // B0-B8 registers
};
static_assert(sizeof(ChannelConfigNote) == 2*sizeof(uint8_t));

struct ChannelConfigSynthesis {
  uint8_t outch_d: 1,
    outch_c: 1,
    outch_r: 1,
    outch_l: 1,
    feedback: 3,
    synthtype: 1;

  uint8_t value() const {
    return (outch_d << 7) | (outch_c << 6) | (outch_r << 5) | (outch_l << 4) | (feedback << 1) | synthtype;
  }
};
static_assert(sizeof(ChannelConfigSynthesis) == sizeof(uint8_t));

static const float kBlockHighestHz[] = {
  48.503,
  97.006,
  194.013,
  388.026,
  776.053,
  1552.107,
  3104.215,
  6208.431
};
static const float kBlockIntervalHz[] = {
  0.048,
  0.095,
  0.190,
  0.379,
  0.759,
  1.517,
  3.034,
  6.069
};

struct Note {
  uint8_t freqLo;
  uint8_t freqHi;
  uint8_t block;

  float CV2Hz(float cv) {
    return 261.6256f * pow(2, cv);
  }

  bool computeOPLParamsFromCV(float cv) {
    float frequency = CV2Hz(cv);
    block = 0;
    for (auto maxf : kBlockHighestHz) {
      if (frequency < maxf) {
	float interval = kBlockIntervalHz[block];
	uint32_t freqBits = (uint32_t)(frequency / interval);
	freqLo = freqBits & 0xff;
	freqHi = (freqBits >> 8) & 0x3;
	return true;
      }
      block++;
    }
    return false;
  }
};

struct YM38126x4 : Module {
  enum ParamIds {
    ALGORITHM_PARAM,
    TREMOLO_1_PARAM, TREMOLO_2_PARAM, TREMOLO_3_PARAM, TREMOLO_4_PARAM,
    VIBRATO_1_PARAM, VIBRATO_2_PARAM, VIBRATO_3_PARAM, VIBRATO_4_PARAM,
    SUSTAIN_TOGGLE_1_PARAM, SUSTAIN_TOGGLE_2_PARAM, SUSTAIN_TOGGLE_3_PARAM, SUSTAIN_TOGGLE_4_PARAM,
    KSR_1_PARAM, KSR_2_PARAM, KSR_3_PARAM, KSR_4_PARAM,
    MULTI_1_PARAM, MULTI_2_PARAM, MULTI_3_PARAM, MULTI_4_PARAM,
    KSL_1_PARAM, KSL_2_PARAM, KSL_3_PARAM, KSL_4_PARAM,
    ATTENUATION_1_PARAM, ATTENUATION_2_PARAM, ATTENUATION_3_PARAM, ATTENUATION_4_PARAM,
    WAVEFORM_1_PARAM, WAVEFORM_2_PARAM, WAVEFORM_3_PARAM, WAVEFORM_4_PARAM,
    ATTACK_1_PARAM, ATTACK_2_PARAM, ATTACK_3_PARAM, ATTACK_4_PARAM,
    DECAY_1_PARAM, DECAY_2_PARAM, DECAY_3_PARAM, DECAY_4_PARAM,
    SUSTAIN_1_PARAM, SUSTAIN_2_PARAM, SUSTAIN_3_PARAM, SUSTAIN_4_PARAM,
    RELEASE_1_PARAM, RELEASE_2_PARAM, RELEASE_3_PARAM, RELEASE_4_PARAM,
    NUM_PARAMS
  };
  enum InputIds {
    GATE_0_INPUT,
    CV_0_INPUT = GATE_0_INPUT + 6,
    NUM_INPUTS = CV_0_INPUT + 6
  };
  enum OutputIds {
    LEFT_OUTPUT,
    RIGHT_OUTPUT,
    NUM_OUTPUTS
  };
  enum LightIds {
    NUM_LIGHTS
  };

  // CEmuopl opl_;
  DBOPL::Handler opl_;
  
  YM38126x4() :
    Module(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS)//,
    // opl_(44100 /* rate */, true /* use16bit */, true /* usestereo */) // TODO test different rate
  {
    // In 6x4 mode we enable 6 4-op channels.
    // For each channel, there are 4 operators (ops: A, B, C, D)
    // 0 (ops: 0, 3, 6, 9)
    // 1 (ops: 1, 4, 7, 10)
    // 2 (ops: 2, 5, 8, 11)
    // 9 (ops: 18, 21, 24, 27)
    // 10 (ops: 19, 22, 25, 28)
    // 11 (ops: 20, 23, 26, 29)
    // We treat all voices as the same instrument, so it's a single 6-voices instrument.
    // This means that all writes that affect an operator are done 6 times, for each operator.

    opl_.Init(44100);
    
    // Init code
    for (unsigned int i = 0x00; i < 0x300; ++i) {
      opl_.WriteReg(i, 0x00);
    }
    opl_.WriteReg(0x01, 1<<5); // Enable waveform selection per operator
    opl_.WriteReg(0x105, 0x01); // Enable OPL3 features
    opl_.WriteReg(0x104, 0xff); // Enable 4-OP for all 6 channels
  }

  // void reset() { OPLResetChip(opl_); } // TODO something like that

  static unsigned int OperatorRegister(unsigned base, unsigned int op) {
    // Operator registers are not continuous. This is a formula
    // derived from http://www.shikadi.net/moddingwiki/OPL_chip that
    // computes the register ID for a given operator.
    if (op < 6) return base + op;
    else if (op < 12) return base + op + 0x02;
    else if (op < 18) return base + op + 0x04;
    else if (op < 24) return base + op + 0x100 - 18;
    else if (op < 30) return base + op + 0x102 - 18;
    else return base + op + 0x104 - 18;
  }

  static unsigned int ChannelRegister(unsigned int base, unsigned int ch) {
    if (ch < 9) return base + ch;
    else return base + ch + 0x100 - 9;
  }

  void writeRegister(unsigned int reg, uint8_t value) {
    opl_.WriteReg(reg, value);
  }

  uint8_t scaleParam(int param, float maxvalue, uint8_t mask) {
    float value = params[param].value / maxvalue;
    return static_cast<uint8_t>(round((float)mask * value));
  }
  
  unsigned int nstep = 0;

  void step() override {
    nstep++;

    //// Configure the chip
    // 21	Operator 1	Tremolo/Vibrato/Sustain/KSR/Multiplication
    // 24	Operator 2	Tremolo/Vibrato/Sustain/KSR/Multiplication
    // 29	Operator 3	Tremolo/Vibrato/Sustain/KSR/Multiplication
    // 2C	Operator 4	Tremolo/Vibrato/Sustain/KSR/Multiplication
    if (nstep == 1) {
      for (unsigned int op = 0; op < 4; ++op) {
	OperatorConfigEffects o{
	tremolo: params[TREMOLO_1_PARAM + op].value > 0.5f,
	    vibrato: params[VIBRATO_1_PARAM + op].value > 0.5f,
	    sustain: params[SUSTAIN_TOGGLE_1_PARAM + op].value > 0.5f,
	    ksr: params[KSR_1_PARAM + op].value > 0.5f,
	    multi: scaleParam(MULTI_1_PARAM + op, 15.0, 0xf), //0xf & static_cast<unsigned int>(params[MULTI_1_PARAM + op].value),
	    };
	for (unsigned int ch = 0; ch < 6; ++ch) {
	  writeRegister(OperatorRegister(0x20, kHWOperatorForChannel[ch] + 3*op), o.value());
	}
      }
    }

    // 41	Operator 1	Key Scale Level/Output Level
    // 44	Operator 2	Key Scale Level/Output Level
    // 49	Operator 3	Key Scale Level/Output Level
    // 4C	Operator 4	Key Scale Level/Output Level
    if (nstep == 2) {
      for (unsigned int op = 0; op < 4; ++op) {
	OperatorConfigLevels o{
	ksl: 0xf & (unsigned int)params[KSL_1_PARAM + op].value,
	    level: 0xf & (unsigned int)params[ATTENUATION_1_PARAM + op].value,
	    };
	for (unsigned int ch = 0; ch < 6; ++ch) {
	  writeRegister(OperatorRegister(0x40, kHWOperatorForChannel[ch] + 3*op), o.value());
	}
      }
    }

    // 61	Operator 1	Attack Rate/Decay Rate
    // 64	Operator 2	Attack Rate/Decay Rate
    // 69	Operator 3	Attack Rate/Decay Rate
    // 6C	Operator 4	Attack Rate/Decay Rate
    if (nstep == 3) {
      for (unsigned int op = 0; op < 4; ++op) {
	OperatorConfigAtkDec o{
	attack: 0xf & (unsigned int)params[ATTACK_1_PARAM + op].value,
	    decay: 0xf & (unsigned int)params[DECAY_1_PARAM + op].value,
	    };
	for (unsigned int ch = 0; ch < 6; ++ch) {
	  writeRegister(OperatorRegister(0x60, kHWOperatorForChannel[ch] + 3*op), o.value());
	}
      }
    }

    // 81	Operator 1	Sustain Level/Release Rate
    // 84	Operator 2	Sustain Level/Release Rate
    // 89	Operator 3	Sustain Level/Release Rate
    // 8C	Operator 4	Sustain Level/Release Rate
    if (nstep == 4) {
      for (unsigned int op = 0; op < 4; ++op) {
	OperatorConfigSusRel o{
	sustain: 0xf & (unsigned int)params[SUSTAIN_1_PARAM + op].value,
	    release: 0xf & (unsigned int)params[RELEASE_1_PARAM + op].value,
	    };
	for (unsigned int ch = 0; ch < 6; ++ch) {
	  writeRegister(OperatorRegister(0x80, kHWOperatorForChannel[ch] + 3*op), o.value());
	}
      }
    }

    // C1		FeedBack/Synthesis Type (part 1)
    // C4		Synthesis Type (part 2)
    if (nstep == 5) {
      unsigned int algorithm = static_cast<unsigned int>(clampf(params[ALGORITHM_PARAM].value, 0.0f, 4.0f));
      uint8_t feedback = 0x00; // TODO: implement feedback. Only affects operator 1 of each algorithm, ignored for other operators.

      ChannelConfigSynthesis c_primary{
      outch_d: false,  // We don't use the extra channels C and D
	  outch_c: false,
	  outch_r: true, // TODO?
	  outch_l: true,
	  feedback: feedback, // TODO!
	  synthtype: algorithm & 1,
	  };
      ChannelConfigSynthesis c_secondary{
      outch_d: false,
	  outch_c: false,
	  outch_r: false, // L, R and feedback are ignored for secondary channel config
	  outch_l: false,
	  feedback: 0,
	  synthtype: (algorithm & 2) >> 1,
	  };
      for (unsigned int ch = 0; ch < kChannels; ++ch) {
	writeRegister(ChannelRegister(0xC0, kHWChannels[ch]), c_primary.value()); // Set synthesis type & feedback for channel 0
	writeRegister(ChannelRegister(0xC0, kHWChannels[ch] + 3), c_secondary.value()); // Same for shadow channel 3
      }
    }

    // E1	Operator 1	Waveform Select
    // E4	Operator 2	Waveform Select
    // E9	Operator 3	Waveform Select
    // EC	Operator 4	Waveform Select
    if (nstep == 6) {
      for (unsigned int op = 0; op < 4; ++op) {
	OperatorConfigWaveform o{
	waveform: params[WAVEFORM_1_PARAM + op].value,
	    };
	for (unsigned int ch = 0; ch < 6; ++ch) {
	  writeRegister(OperatorRegister(0xE0, kHWOperatorForChannel[ch] + 3*op), o.value());
	}
      }
    }

    // A1		Frequency Number (low)
    // A4		Unused
    // B1		Key On/Block Number/Frequency Number (high)
    // B4		Unused
    if (nstep == 7) {
      for (unsigned int ch = 0; ch < kChannels; ++ch) {
	Note n;
	if (n.computeOPLParamsFromCV(inputs[CV_0_INPUT + ch].value)) {
	  ChannelConfigNote o{};
	  o.A.freqlow8bits = n.freqLo;

	  o.B.keyon = (inputs[GATE_0_INPUT + ch].value > 1.0f); // TODO
	  o.B.block = n.block;
	  o.B.freqhi2bits = n.freqHi;

	  writeRegister(ChannelRegister(0xA0, kHWChannels[ch]), o.A.value());
	  writeRegister(ChannelRegister(0xA0, kHWChannels[ch] + 3), o.A.value());
	  writeRegister(ChannelRegister(0xB0, kHWChannels[ch]), o.B.value());
	  writeRegister(ChannelRegister(0xB0, kHWChannels[ch] + 3), o.B.value());
	}
      }
    }

    if (nstep >= 8) {
      if (nstep >= 32) {
	nstep = 0;
      }
    }
    
    //// Synthesize sound
    static const int kSamples = 1;
    int32_t buf[kSamples * 2]; // 2 channels
    //opl_.update(buf, samples);
    opl_.chip.GenerateBlock3(kSamples, buf);
    outputs[LEFT_OUTPUT].value = (float)buf[0] / (float)0x7fff * 10.f;
    outputs[RIGHT_OUTPUT].value = (float)buf[1] / (float)0x7fff * 10.f;
  }
};

struct SnappyKnob : Davies1900hBlackKnob {
  SnappyKnob() : Davies1900hBlackKnob() {
    snap = true;
  }
};

struct YM38126x4Widget : ModuleWidget {
  YM38126x4* module_;

  YM38126x4Widget(YM38126x4 *module) : ModuleWidget(module), module_(module) {
    setPanel(SVG::load(assetPlugin(plugin, "res/YM38126x4.svg")));

    addChild(Widget::create<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
    addChild(Widget::create<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
    addChild(Widget::create<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
    addChild(Widget::create<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

    // Operator block
    for (unsigned int i = 0; i < 4; ++i) {
      // Line 1: OperatorConfigEffects
      addParam(ParamWidget::create<BefacoSwitch>(Vec(175*i + 15, 20), module, YM38126x4::TREMOLO_1_PARAM + i, 0.0, 1.0, 0.0));
      addParam(ParamWidget::create<BefacoSwitch>(Vec(175*i + 45, 20), module, YM38126x4::VIBRATO_1_PARAM + i, 0.0, 1.0, 0.0));
      addParam(ParamWidget::create<BefacoSwitch>(Vec(175*i + 75, 20), module, YM38126x4::SUSTAIN_TOGGLE_1_PARAM + i, 0.0, 1.0, 0.0));
      addParam(ParamWidget::create<BefacoSwitch>(Vec(175*i + 105, 20), module, YM38126x4::KSR_1_PARAM + i, 0.0, 1.0, 0.0));
      addParam(ParamWidget::create<SnappyKnob>(Vec(175*i + 135, 20), module, YM38126x4::MULTI_1_PARAM + i, 0.0, 15.0, 0.0));

      // Line 2: OperatorConfigLevels + OperatorConfigWaveform
      addParam(ParamWidget::create<SnappyKnob>(Vec(175*i + 15, 70), module, YM38126x4::KSL_1_PARAM + i, 0.0, 15.0, 0.0));
      addParam(ParamWidget::create<SnappyKnob>(Vec(175*i + 75, 70), module, YM38126x4::ATTENUATION_1_PARAM + i, 0.0, 15.0, 0.0));
      addParam(ParamWidget::create<SnappyKnob>(Vec(175*i + 135, 70), module, YM38126x4::WAVEFORM_1_PARAM + i, 0.0, 7.0, 0.0));

      // Line 3: ADSR
      addParam(ParamWidget::create<SnappyKnob>(Vec(175*i + 15, 130), module, YM38126x4::ATTACK_1_PARAM + i, 0.0, 15.0, 0.0));
      addParam(ParamWidget::create<SnappyKnob>(Vec(175*i + 55, 130), module, YM38126x4::DECAY_1_PARAM + i, 0.0, 15.0, 0.0));
      addParam(ParamWidget::create<SnappyKnob>(Vec(175*i + 95, 130), module, YM38126x4::SUSTAIN_1_PARAM + i, 0.0, 15.0, 0.0));
      addParam(ParamWidget::create<SnappyKnob>(Vec(175*i + 135, 130), module, YM38126x4::RELEASE_1_PARAM + i, 0.0, 15.0, 0.0));
    }

    // Algorithm selection knob
    auto* p = ParamWidget::create<Davies1900hBlackKnob>(Vec(20, 340), module, YM38126x4::ALGORITHM_PARAM, 0.0, 3.0, 0.0);
    p->snap = true;
    addParam(p);

    // Gate+CV inputs
    for (unsigned int i = 0; i < 6; ++i) {
      addInput(Port::create<PJ301MPort>(Vec(80 + i * 30, 240), Port::INPUT, module, YM38126x4::GATE_0_INPUT + i));
      addInput(Port::create<PJ301MPort>(Vec(80 + i * 30, 270), Port::INPUT, module, YM38126x4::CV_0_INPUT + i));
    }


    // Output
    addOutput(Port::create<PJ301MPort>(Vec(20, 300), Port::OUTPUT, module, YM38126x4::LEFT_OUTPUT));
    addOutput(Port::create<PJ301MPort>(Vec(40, 320), Port::OUTPUT, module, YM38126x4::RIGHT_OUTPUT));
  }
};

Model *modelYM38126x4 = Model::create<YM38126x4, YM38126x4Widget>("YMod3812", "0x02-6x4", "YM3812 4 voices FM synthesizer", OSCILLATOR_TAG);
