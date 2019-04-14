#include "OPL33t.hpp"
#include "dsp/digital.hpp"
#include "utils/bidischmitttrigger.hpp"
#include "utils/componentlibrary.hpp"
#include "oplregisters.hpp"
#include <list>

// #include <iostream>
// #include <iomanip>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wswitch"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#include "deps/adlmidi/src/dbopl.h"
#pragma GCC diagnostic pop

static const unsigned int kGenericLearnableParams = 6;
static const unsigned int kPerChannelLearnableParams = 2;
static const unsigned int kTotalLearnableParams = kGenericLearnableParams + kPerChannelLearnableParams;

struct FM6x4 : Module {
  enum ParamIds {
    ALGORITHM_PARAM,
    ENUMS(TREMOLO_PARAM, OPL3::FourOP::kOperatorsPerChannel),
    ENUMS(VIBRATO_PARAM, OPL3::FourOP::kOperatorsPerChannel),
    ENUMS(SUSTAIN_TOGGLE_PARAM, OPL3::FourOP::kOperatorsPerChannel),
    ENUMS(KSR_PARAM, OPL3::FourOP::kOperatorsPerChannel),
    ENUMS(MULTI_PARAM, OPL3::FourOP::kOperatorsPerChannel),
    ENUMS(KSL_PARAM, OPL3::FourOP::kOperatorsPerChannel),
    ENUMS(ATTENUATION_PARAM, OPL3::FourOP::kOperatorsPerChannel),
    ENUMS(WAVEFORM_PARAM, OPL3::FourOP::kOperatorsPerChannel),
    ENUMS(ATTACK_PARAM, OPL3::FourOP::kOperatorsPerChannel),
    ENUMS(DECAY_PARAM, OPL3::FourOP::kOperatorsPerChannel),
    ENUMS(SUSTAIN_PARAM, OPL3::FourOP::kOperatorsPerChannel),
    ENUMS(RELEASE_PARAM, OPL3::FourOP::kOperatorsPerChannel),

    NUM_SAVEABLE_PARAMS, // The params below this can't be saved/automated by CV

    ENUMS(LEARN_PARAM, kTotalLearnableParams),
    UNLEARN_PARAM,
    NUM_PARAMS
  };
  enum InputIds {
    ENUMS(GATE_INPUT, OPL3::kChannels),
    ENUMS(CV_INPUT, OPL3::kChannels),
    ENUMS(GENERIC_PARAMETER_INPUT, OPL3::kChannels),
    ENUMS(PER_CHANNEL_PARAMETER_A_INPUT, OPL3::kChannels),
    ENUMS(PER_CHANNEL_PARAMETER_B_INPUT, OPL3::kChannels),
    NUM_INPUTS
  };
  enum OutputIds {
    LEFT_OUTPUT,
    RIGHT_OUTPUT,
    NUM_OUTPUTS
  };
  enum LightIds {
    ENUMS(LEARNED_PARAM_LIGHT, NUM_SAVEABLE_PARAMS * 3),
    LEARNING_LIGHT_R,
    LEARNING_LIGHT_G,
    LEARNING_LIGHT_B,
    NUM_LIGHTS
  };

  DBOPL::Handler opl_;

  // Parameter learning stuff
  enum LearningStatus {
    NOT_LEARNING,
    ENUMS(LEARNING, kTotalLearnableParams),
    UNLEARNING
  };
  LearningStatus learningStatus_ = LearningStatus::NOT_LEARNING;
  int learnedParams[NUM_SAVEABLE_PARAMS];
  SchmittTrigger learningButton[kTotalLearnableParams];
  SchmittTrigger unlearningButton;
  BidiSchmittTrigger keyOn[OPL3::kChannels];
  unsigned int nstep = 0;
  float paramsSavedValues[NUM_SAVEABLE_PARAMS];

  float kColorForLearningChannel[10][3] = {
    {0.0f, 0.0f, 0.0f}, // NOT_LEARNING
    {1.0f, 0.0f, 0.0f}, // LEARNING 0 through 7
    {0.0f, 1.0f, 0.0f},
    {0.0f, 0.0f, 1.0f},
    {1.0f, 1.0f, 0.0f},
    {0.0f, 1.0f, 1.0f},
    {1.0f, 0.0f, 1.0f},
    {1.0f, 0.3f, 0.3f},
    {0.3f, 0.3f, 1.0f},
    {1.0f, 1.0f, 1.0f}, // UNLEARNING
  };

  FM6x4() :
    Module(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS)
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

    opl_.Init(44100); // TODO handle other rates!

    // Init code
    for (unsigned int i = 0x00; i < 0x300; ++i) {
      opl_.WriteReg(i, 0x00);
    }
    opl_.WriteReg(0x01, 1<<5); // Enable waveform selection per operator
    opl_.WriteReg(0x105, 0x01); // Enable OPL3 features
    opl_.WriteReg(0x104, 0xff); // Enable 4-OP for all 6 channels

    for (auto& lp : learnedParams) {
      lp = -1;
    }
  }

  void reset() override {
    opl_.Init(44100); // TODO handle other rates!
  }

  void writeRegister(unsigned int reg, uint8_t value) {
    opl_.WriteReg(reg, value);
  }

  uint8_t getScaledParam(int param, float maxvalue, uint8_t mask, unsigned int channel) {
    float value = 0.f;
    if (learnedParams[param] != -1) {
      if (learnedParams[param] == 6) { // "parameter CVs" 6 and 7 are per-channel
	value = clamp(inputs[PER_CHANNEL_PARAMETER_A_INPUT + channel].value, 0.f, 10.f);
      } else if (learnedParams[param] == 7) {
	value = clamp(inputs[PER_CHANNEL_PARAMETER_B_INPUT + channel].value, 0.f, 10.f);
      } else {
	value = clamp(inputs[GENERIC_PARAMETER_INPUT + learnedParams[param]].value, 0.f, 10.f) / 10.f * maxvalue;
      }
    }
    value += params[param].value;
    value = clamp(value, 0.f, 10.f);
    value /= maxvalue;
    if (value < 0.f) {
      return (uint8_t)(0);
    }
    return static_cast<uint8_t>(round((float)mask * value));
  }

  void saveAllParams() {
    for (unsigned int i = 0; i < NUM_SAVEABLE_PARAMS; ++i) {
      paramsSavedValues[i] = params[i].value;
    }
  }

  void setLearningStatus(LearningStatus l) {
    learningStatus_ = l;
  }

  void step() override {
    nstep++;

    // Learning params
    for (int i = 0; i < 8; ++i) {
      if (learningButton[i].process(params[LEARN_PARAM + i].value)) {
	if (learningStatus_ == LearningStatus::LEARNING + i) {
	  setLearningStatus(LearningStatus::NOT_LEARNING);
	} else {
	  setLearningStatus((LearningStatus)(LearningStatus::LEARNING + i));
	  saveAllParams();
	}
      }
    }
    if (unlearningButton.process(params[UNLEARN_PARAM].value)) {
      if (learningStatus_ == LearningStatus::UNLEARNING) {
	setLearningStatus(LearningStatus::NOT_LEARNING);
      } else {
	setLearningStatus(LearningStatus::UNLEARNING);
	saveAllParams();
      }
    }
    if (learningStatus_ != LearningStatus::NOT_LEARNING) {
      for (unsigned int p = 0; p < NUM_SAVEABLE_PARAMS; ++p) {
	if (paramsSavedValues[p] != params[p].value) {
	  if (learningStatus_ == LearningStatus::UNLEARNING) {
	    learnedParams[p] = -1;
	    lights[LEARNED_PARAM_LIGHT + p * 3].setBrightness(0.f);
	    lights[LEARNED_PARAM_LIGHT + p * 3 + 1].setBrightness(0.f);
	    lights[LEARNED_PARAM_LIGHT + p * 3 + 2].setBrightness(0.f);
	  } else {
	    unsigned int s = learningStatus_ - LearningStatus::LEARNING;
	    learnedParams[p] = s;
	    lights[LEARNED_PARAM_LIGHT + p * 3].setBrightness(kColorForLearningChannel[learningStatus_][0]);
	    lights[LEARNED_PARAM_LIGHT + p * 3 + 1].setBrightness(kColorForLearningChannel[learningStatus_][1]);
	    lights[LEARNED_PARAM_LIGHT + p * 3 + 2].setBrightness(kColorForLearningChannel[learningStatus_][2]);
	  }
	  setLearningStatus(LearningStatus::NOT_LEARNING);
	  break;
	}
      }
    }
    lights[LEARNING_LIGHT_R].setBrightness(kColorForLearningChannel[learningStatus_][0]);
    lights[LEARNING_LIGHT_G].setBrightness(kColorForLearningChannel[learningStatus_][1]);
    lights[LEARNING_LIGHT_B].setBrightness(kColorForLearningChannel[learningStatus_][2]);
    
    //// Configure the chip
    // 21	Operator 1	Tremolo/Vibrato/Sustain/KSR/Multiplication
    // 24	Operator 2	Tremolo/Vibrato/Sustain/KSR/Multiplication
    // 29	Operator 3	Tremolo/Vibrato/Sustain/KSR/Multiplication
    // 2C	Operator 4	Tremolo/Vibrato/Sustain/KSR/Multiplication
    if (nstep == 1) {
      for (unsigned int op = 0; op < 4; ++op) {
	for (unsigned int ch = 0; ch < 6; ++ch) {
	  OPL3::OperatorConfigEffects o{
	  tremolo: getScaledParam(TREMOLO_PARAM + op, 1.0, 0x1, ch),
	      vibrato: getScaledParam(VIBRATO_PARAM + op, 1.0, 0x1, ch),
	      sustain: getScaledParam(SUSTAIN_TOGGLE_PARAM + op, 1.0, 0x1, ch),
	      ksr: getScaledParam(KSR_PARAM + op, 1.0, 0x1, ch),
	      multi: getScaledParam(MULTI_PARAM + op, 15.0, 0xf, ch),
	      };
	  writeRegister(OPL3::OperatorRegister(0x20, OPL3::FourOP::kHWOperatorForChannel[ch] + 3*op), o.value());
	}
      }
    }

    // 41	Operator 1	Key Scale Level/Output Level
    // 44	Operator 2	Key Scale Level/Output Level
    // 49	Operator 3	Key Scale Level/Output Level
    // 4C	Operator 4	Key Scale Level/Output Level
    if (nstep == 2) {
      for (unsigned int op = 0; op < 4; ++op) {
	for (unsigned int ch = 0; ch < 6; ++ch) {
	  OPL3::OperatorConfigLevels o{
	  ksl: getScaledParam(KSL_PARAM + op, 1.0, 0x1, ch),
	      level: getScaledParam(ATTENUATION_PARAM + op, 1.0, 0x1, ch),
	      };
	  writeRegister(OPL3::OperatorRegister(0x40, OPL3::FourOP::kHWOperatorForChannel[ch] + 3*op), o.value());
	}
      }
    }

    // 61	Operator 1	Attack Rate/Decay Rate
    // 64	Operator 2	Attack Rate/Decay Rate
    // 69	Operator 3	Attack Rate/Decay Rate
    // 6C	Operator 4	Attack Rate/Decay Rate
    if (nstep == 3) {
      for (unsigned int op = 0; op < 4; ++op) {
	for (unsigned int ch = 0; ch < 6; ++ch) {
	  OPL3::OperatorConfigAtkDec o{
	  attack: getScaledParam(ATTACK_PARAM + op, 15.0, 0xf, ch),
	      decay: getScaledParam(DECAY_PARAM + op, 15.0, 0xf, ch),
	      };
	  writeRegister(OPL3::OperatorRegister(0x60, OPL3::FourOP::kHWOperatorForChannel[ch] + 3*op), o.value());
	}
      }
    }

    // 81	Operator 1	Sustain Level/Release Rate
    // 84	Operator 2	Sustain Level/Release Rate
    // 89	Operator 3	Sustain Level/Release Rate
    // 8C	Operator 4	Sustain Level/Release Rate
    if (nstep == 4) {
      for (unsigned int op = 0; op < 4; ++op) {
	for (unsigned int ch = 0; ch < 6; ++ch) {
	  OPL3::OperatorConfigSusRel o{
	  sustain: getScaledParam(SUSTAIN_PARAM + op, 15.0, 0xf, ch),
	      release: getScaledParam(RELEASE_PARAM + op, 15.0, 0xf, ch),
	      };
	  writeRegister(OPL3::OperatorRegister(0x80, OPL3::FourOP::kHWOperatorForChannel[ch] + 3*op), o.value());
	}
      }
    }

    // C1		FeedBack/Synthesis Type (part 1)
    // C4		Synthesis Type (part 2)
    if (nstep == 5) {
      unsigned int algorithm = static_cast<unsigned int>(clamp(params[ALGORITHM_PARAM].value, 0.0f, 4.0f));
      uint8_t feedback = 0x00; // TODO: implement feedback. Only affects operator 1 of each algorithm, ignored for other operators.

      OPL3::ChannelConfigSynthesis c_primary{
      outch_d: false,  // We don't use the extra channels C and D
	  outch_c: false,
	  outch_r: true, // TODO?
	  outch_l: true,
	  feedback: feedback, // TODO!
	  synthtype: (uint8_t)(algorithm & 1), // safe cast: x&1 fits on 1 bit
	  };
      OPL3::ChannelConfigSynthesis c_secondary{
      outch_d: false,
	  outch_c: false,
	  outch_r: false, // L, R and feedback are ignored for secondary channel config
	  outch_l: false,
	  feedback: 0,
	  synthtype: (uint8_t)((algorithm & 2) >> 1), // safe cast: ((x>>2)&1) fits on 1 bit
	  };
      for (unsigned int ch = 0; ch < OPL3::kChannels; ++ch) {
	writeRegister(OPL3::ChannelRegister(0xC0, OPL3::FourOP::kHWChannels[ch]), c_primary.value()); // Set synthesis type & feedback for channel 0
	writeRegister(OPL3::ChannelRegister(0xC0, OPL3::FourOP::kHWChannels[ch] + 3), c_secondary.value()); // Same for shadow channel 3
      }
    }

    // E1	Operator 1	Waveform Select
    // E4	Operator 2	Waveform Select
    // E9	Operator 3	Waveform Select
    // EC	Operator 4	Waveform Select
    if (nstep == 6) {
      for (unsigned int op = 0; op < 4; ++op) {
	for (unsigned int ch = 0; ch < 6; ++ch) {
	  OPL3::OperatorConfigWaveform o{
	  waveform: getScaledParam(WAVEFORM_PARAM + op, 7.0f, 0x7, ch),
	      };
	  writeRegister(OPL3::OperatorRegister(0xE0, OPL3::FourOP::kHWOperatorForChannel[ch] + 3*op), o.value());
	}
      }
    }

    // A1		Frequency Number (low)
    // A4		Unused
    // B1		Key On/Block Number/Frequency Number (high)
    // B4		Unused
    if (nstep == 7) {
      for (unsigned int ch = 0; ch < OPL3::kChannels; ++ch) {
	if (keyOn[ch].process(inputs[GATE_INPUT + ch].value)) {
	  OPL3::Note n{};
	  bool send_keyon = keyOn[ch].state && n.computeOPLParamsFromCV(inputs[CV_INPUT + ch].value);
	  OPL3::ChannelConfigNote o{};
	  o.A.freqlow8bits = n.freqLo;
	  o.B.keyon = send_keyon;
	  o.B.block = n.block;
	  o.B.freqhi2bits = n.freqHi;

	  writeRegister(OPL3::ChannelRegister(0xA0, OPL3::FourOP::kHWChannels[ch]), o.A.value());
	  writeRegister(OPL3::ChannelRegister(0xA0, OPL3::FourOP::kHWChannels[ch] + 3), o.A.value());
	  writeRegister(OPL3::ChannelRegister(0xB0, OPL3::FourOP::kHWChannels[ch]), o.B.value());
	  writeRegister(OPL3::ChannelRegister(0xB0, OPL3::FourOP::kHWChannels[ch] + 3), o.B.value());
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
    opl_.chip.GenerateBlock3(kSamples, buf);
    outputs[LEFT_OUTPUT].value = (float)buf[0] / (float)0x7fff * 10.f;
    outputs[RIGHT_OUTPUT].value = (float)buf[1] / (float)0x7fff * 10.f;
  }
};

struct FM6x4Widget : ModuleWidget {
  FM6x4* module_;

  template<typename T>
  void addLearnableParam(Vec v, FM6x4* module, int param, float min, float max, float defaultf) {
    addParam(ParamWidget::create<T>(v, module, param, min, max, defaultf));
    addChild(ModuleLightWidget::create<SmallLight<BorderLEDLight<RGBLight>>>(v.plus(Vec(2, -2)), module, FM6x4::LEARNED_PARAM_LIGHT + param * 3));
  }

  FM6x4Widget(FM6x4 *module) : ModuleWidget(module), module_(module) {
    setPanel(SVG::load(assetPlugin(plugin, "res/FM6x4.svg")));

    addChild(Widget::create<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
    addChild(Widget::create<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
    addChild(Widget::create<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
    addChild(Widget::create<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

    // Operator block
    for (unsigned int i = 0; i < 4; ++i) {
      // Line 1: OperatorConfigEffects
      addLearnableParam<BefacoSwitch>(Vec(175*i + 15, 20), module, FM6x4::TREMOLO_PARAM + i, 0.0, 1.0, 0.0);
      addLearnableParam<BefacoSwitch>(Vec(175*i + 45, 20), module, FM6x4::VIBRATO_PARAM + i, 0.0, 1.0, 0.0);
      addLearnableParam<BefacoSwitch>(Vec(175*i + 75, 20), module, FM6x4::SUSTAIN_TOGGLE_PARAM + i, 0.0, 1.0, 0.0);
      addLearnableParam<BefacoSwitch>(Vec(175*i + 105, 20), module, FM6x4::KSR_PARAM + i, 0.0, 1.0, 0.0);
      addLearnableParam<SnappyKnob<Davies1900hBlackKnob>>(Vec(175*i + 135, 20), module, FM6x4::MULTI_PARAM + i, 0.0, 15.0, 0.0);

      // Line 2: OperatorConfigLevels + OperatorConfigWaveform
      addLearnableParam<SnappyKnob<Davies1900hBlackKnob>>(Vec(175*i + 15, 70), module, FM6x4::KSL_PARAM + i, 0.0, 15.0, 0.0);
      addLearnableParam<SnappyKnob<Davies1900hBlackKnob>>(Vec(175*i + 75, 70), module, FM6x4::ATTENUATION_PARAM + i, 0.0, 15.0, 0.0);
      addLearnableParam<SnappyKnob<Davies1900hBlackKnob>>(Vec(175*i + 135, 70), module, FM6x4::WAVEFORM_PARAM + i, 0.0, 7.0, 0.0);

      // Line 3: ADSR
      addLearnableParam<SnappyKnob<Davies1900hBlackKnob>>(Vec(175*i + 15, 130), module, FM6x4::ATTACK_PARAM + i, 0.0, 15.0, 0.0);
      addLearnableParam<SnappyKnob<Davies1900hBlackKnob>>(Vec(175*i + 55, 130), module, FM6x4::DECAY_PARAM + i, 0.0, 15.0, 0.0);
      addLearnableParam<SnappyKnob<Davies1900hBlackKnob>>(Vec(175*i + 95, 130), module, FM6x4::SUSTAIN_PARAM + i, 0.0, 15.0, 0.0);
      addLearnableParam<SnappyKnob<Davies1900hBlackKnob>>(Vec(175*i + 135, 130), module, FM6x4::RELEASE_PARAM + i, 0.0, 15.0, 0.0);
    }

    // Algorithm selection knob
    auto* p = ParamWidget::create<Davies1900hBlackKnob>(Vec(20, 340), module, FM6x4::ALGORITHM_PARAM, 0.0, 3.0, 0.0);
    p->snap = true;
    addParam(p);

    // Gate+pitch CV+per channel CV parameter inputs
    for (unsigned int i = 0; i < 6; ++i) {
      addInput(Port::create<PJ301MPort>(Vec(80 + i * 30, 240), Port::INPUT, module, FM6x4::GATE_INPUT + i));
      addInput(Port::create<PJ301MPort>(Vec(80 + i * 30, 270), Port::INPUT, module, FM6x4::CV_INPUT + i));
      addInput(Port::create<PJ301MPort>(Vec(80 + i * 30, 300), Port::INPUT, module, FM6x4::PER_CHANNEL_PARAMETER_A_INPUT + i));
      addInput(Port::create<PJ301MPort>(Vec(80 + i * 30, 330), Port::INPUT, module, FM6x4::PER_CHANNEL_PARAMETER_B_INPUT + i));
    }
    // Learn buttons for per-channel inputs
    addParam(ParamWidget::create<CKD6>(Vec(260, 300), module, FM6x4::LEARN_PARAM + 6, -1.0f, 1.0f, -1.0f));
    addParam(ParamWidget::create<CKD6>(Vec(260, 330), module, FM6x4::LEARN_PARAM + 7, -1.0f, 1.0f, -1.0f));

    // CV parameter inputs
    for (unsigned int i = 0; i < 6; ++i) {
      addInput(Port::create<PJ301MPort>(Vec(300 + i * 30, 280), Port::INPUT, module, FM6x4::GENERIC_PARAMETER_INPUT + i));
      addParam(ParamWidget::create<CKD6>(Vec(300 + i * 30, 310), module, FM6x4::LEARN_PARAM + i, -1.0f, 1.0f, -1.0f));
    }
    // unlearn button
    addParam(ParamWidget::create<CKD6>(Vec(300 + 6 * 30, 310), module, FM6x4::UNLEARN_PARAM, -1.0f, 1.0f, -1.0f));
    // Learn status LED
    addChild(ModuleLightWidget::create<LargeLight<BorderLEDLight<RGBLight>>>(Vec(300 + 6 * 30, 280), module, FM6x4::LEARNING_LIGHT_R));

    // Output
    addOutput(Port::create<PJ301MPort>(Vec(20, 300), Port::OUTPUT, module, FM6x4::LEFT_OUTPUT));
    addOutput(Port::create<PJ301MPort>(Vec(40, 320), Port::OUTPUT, module, FM6x4::RIGHT_OUTPUT));
  }
};

Model *model6x4 = Model::create<FM6x4, FM6x4Widget>("OPL33t", "FM6x4", "OPL3-based 6 voices 4 operators FM synthesizer", OSCILLATOR_TAG, DIGITAL_TAG, MULTIPLE_TAG, QUAD_TAG, DUAL_TAG);
