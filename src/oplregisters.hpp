#ifndef OPLREGISTERS_HPP
#define OPLREGISTERS_HPP

namespace OPL3 {

  // This file contains POD-style classes that allows one to populate
  // relevant bits of OPL3 registers, as well as utility functions to
  // use them or to address registers.

  // Configures registers 0x20-0x35.
  // http://www.shikadi.net/moddingwiki/OPL_chip#20-35:_Tremolo_.2F_Vibrato_.2F_Sustain_.2F_KSR_.2F_Frequency_Multiplication_Factor
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
  static_assert(sizeof(OperatorConfigEffects) == sizeof(uint8_t), "Size mismatch");

  // Configures registers 0x40-0x55. These registers are per-operator.
  // http://www.shikadi.net/moddingwiki/OPL_chip#40-55:_Key_Scale_Level_.2F_Output_Level
  struct OperatorConfigLevels {
    uint8_t ksl: 2,
      level: 6;

    uint8_t value() const {
      return (ksl << 6) | level;
    }
  };
  static_assert(sizeof(OperatorConfigLevels) == sizeof(uint8_t), "Size mismatch");

  // Configures registers 0x60-0x75. These registers are per-operator.
  // http://www.shikadi.net/moddingwiki/OPL_chip#60-75:_Attack_Rate_.2F_Decay_Rate
  struct OperatorConfigAtkDec {
    uint8_t attack: 4,
      decay: 4;

    uint8_t value() const {
      return (attack << 4) | decay;
    }
  };
  static_assert(sizeof(OperatorConfigAtkDec) == sizeof(uint8_t), "Size mismatch");

  // Configures registers 0x80-0x95. These registers are per-operator.
  // http://www.shikadi.net/moddingwiki/OPL_chip#80-95:_Sustain_Level_.2F_Release_Rate
  struct OperatorConfigSusRel {
    uint8_t sustain: 4,
      release: 4;

    uint8_t value() const {
      return (sustain << 4) | release;
    }
  };
  static_assert(sizeof(OperatorConfigSusRel) == sizeof(uint8_t), "Size mismatch");

  // Configures registers 0xE0-0xF5. These registers are per-operator.
  // http://www.shikadi.net/moddingwiki/OPL_chip#E0-F5:_Waveform_Select
  struct OperatorConfigWaveform {
    uint8_t waveform: 3;

    uint8_t value() const {
      return waveform;
    }
  };
  static_assert(sizeof(OperatorConfigWaveform) == sizeof(uint8_t), "Size mismatch");

  // Configures registers 0xA0-0xA8 and 0xB0-0xB8. These registers are per-channel.
  // Because B and A registers come together (the frequency at which to
  // play a note is split between the two), these are grouped in a
  // single struct.
  // http://www.shikadi.net/moddingwiki/OPL_chip#A0-A8:_Frequency_Number
  // http://www.shikadi.net/moddingwiki/OPL_chip#B0-B8:_Key_On_.2F_Block_Number_.2F_F-Number.28hi_bits.29
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
  static_assert(sizeof(ChannelConfigNote) == 2*sizeof(uint8_t), "Size mismatch");

  // Configures registers 0xC0-0xC8. These registers are per-channel.
  // http://www.shikadi.net/moddingwiki/OPL_chip#C0-C8:_FeedBack_Modulation_Factor_.2F_Synthesis_Type
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
  static_assert(sizeof(ChannelConfigSynthesis) == sizeof(uint8_t), "Size mismatch");

  // This table lists for each "block" (granularity level) what is the
  // highest pitch that the oscillator can reach. See register 0xB0
  // documentation for an explanation on frequency blocks.
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

  // Similar to kBlockHighestHz, this table lists the interval between 2
  // contiguous pitches for each block.
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

  // This utility class computes the optimal block and frequency (split
  // into low and high bits) for a given pitch CV. The block and frequency fields can be used directly to drive registers 0xA0~0xA8 and 0xB0~0xB8
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

  static const unsigned int kChannels = 6;
  
  namespace FourOP {
    static const unsigned int kOperatorsPerChannel = 4;

    // Mapping to actual hardware channel A (because channels come in A/B
    // pairs when using 4-op synthesis) for each of our virtual channels
    // (1-6). http://www.shikadi.net/moddingwiki/OPL_chip#Register_Map
    static const unsigned int kHWChannels[kChannels] = {0, 1, 2, 9, 10, 11};
  
    // Mapping of virtual channel (0-6) to the first operator for that
    // channel.  http://www.shikadi.net/moddingwiki/OPL_chip#Register_Map
    static const unsigned int kHWOperatorForChannel[kChannels] = {0, 1, 2, 18, 19, 20};
  };

}; // namespace OPL3

#endif
