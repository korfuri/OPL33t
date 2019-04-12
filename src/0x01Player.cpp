#include "Template.hpp"
#include "osdialog.h"


#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wswitch"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#include "deps/adlmidi/dbopl.h"
#pragma GCC diagnostic pop

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsuggest-override"
#pragma GCC diagnostic ignored "-Wignored-qualifiers"
#include "deps/adplug/src/adplug.h"
#pragma GCC diagnostic pop

struct AdPlugOPLCompatibility : Copl {
  DBOPL::Handler dbopl_;
  unsigned int rate_;
  
  AdPlugOPLCompatibility(unsigned int rate) : Copl(), rate_(rate) {
    currType = ChipType::TYPE_OPL3;
    init();
  }

  virtual ~AdPlugOPLCompatibility() {}

  virtual void init() override {
    dbopl_.Init(rate_);
  }

  virtual void write(int reg, int val) override {
    dbopl_.WriteReg(reg, val);
  }

  virtual void update(short* buf, int samples) override {
    int tmpbuf[samples * 2];
    dbopl_.chip.GenerateBlock3((unsigned int)samples, tmpbuf);
    for (int i = 0; i < samples; ++i) {
      buf[i*2] = tmpbuf[i];
      buf[i*2 + 1] = tmpbuf[i];
    }
  }
};

struct YM3812Player : Module {
  enum ParamIds {
    CLOCK_SPEED_PARAM,
    NUM_PARAMS
  };
  enum InputIds {
    CLOCK_SPEED_INPUT,
    NUM_INPUTS
  };
  enum OutputIds {
    LEFT_OUTPUT,
    RIGHT_OUTPUT,
    NUM_OUTPUTS
  };
  enum LightIds {
    NUM_LIGHTS
  };

  //CTemuopl opl_;
  AdPlugOPLCompatibility opl_;
  CPlayer* player_ = nullptr;
  float nextPlayerUpdateIn_ = 0.0f;

  YM3812Player() :
    Module(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS),
    opl_(44100 /* rate */)
  {
  }

  void reset() {
    opl_.init();
    setPlaying("/dev/null");
  }

  void setPlaying(std::string path) {
    if (player_) {
      delete player_;
      player_ = nullptr;
    }
    player_ = CAdPlug::factory(path, &opl_);
    nextPlayerUpdateIn_ = 0.f;
  }

  void step() override {
    // Update player if any
    if (player_) {
      nextPlayerUpdateIn_ -= engineGetSampleTime();
      if (nextPlayerUpdateIn_ <= 0.f) {
	player_->update();
	float overclockspeed = params[CLOCK_SPEED_PARAM].value + inputs[CLOCK_SPEED_INPUT].value;
	nextPlayerUpdateIn_ = 1. / (overclockspeed * player_->getrefresh());
      }
    }

    {
      // Synthesize sound
      static const int samples = 1;
      short buf[samples * 2]; // 2 channels
      opl_.update(buf, samples);
      outputs[LEFT_OUTPUT].value = (float)buf[0] / (float)0x7fff * 10.f;
      outputs[RIGHT_OUTPUT].value = (float)buf[1] / (float)0x7fff * 10.f;
    }
  }
};

static void selectTrackFileAndPlay(YM3812Player* module) {
  // Build a filename filter
  std::string filtersdesc = "All files:*;";
  for (auto const& r : CAdPlug::players) {
    filtersdesc += r->filetype + ":";
    unsigned int i = 0;
    const char* ext;
    while ((ext = r->get_extension(i++))) {
      std::string e = ext + 1; // skip leading '.'
      filtersdesc += e;
      if (r->get_extension(i)) {
	filtersdesc += ",";
      }
    }
    filtersdesc += ";";
  }
  filtersdesc.pop_back(); // remove trailing ';'
  auto filters = osdialog_filters_parse(filtersdesc.c_str());

  // Select a file and load it
  char* path = osdialog_file(OSDIALOG_OPEN, nullptr, nullptr, filters);
  osdialog_filters_free(filters);
  if (path) {
    module->setPlaying(path);
  }
}

struct YM3812PlayerWidget : ModuleWidget {
  YM3812Player* module_;

  YM3812PlayerWidget(YM3812Player *module) : ModuleWidget(module), module_(module) {
    setPanel(SVG::load(assetPlugin(plugin, "res/YM3812Player.svg")));

    addChild(Widget::create<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
    addChild(Widget::create<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
    addChild(Widget::create<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
    addChild(Widget::create<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

    addOutput(Port::create<PJ301MPort>(Vec(20, 300), Port::OUTPUT, module, YM3812Player::LEFT_OUTPUT));
    addOutput(Port::create<PJ301MPort>(Vec(40, 320), Port::OUTPUT, module, YM3812Player::RIGHT_OUTPUT));

    addParam(ParamWidget::create<Davies1900hBlackKnob>(Vec(10, 10), module, YM3812Player::CLOCK_SPEED_PARAM, 0.0, 16.0, 1.0));
    addInput(Port::create<PJ301MPort>(Vec(10, 40), Port::INPUT, module, YM3812Player::CLOCK_SPEED_INPUT));
  }

  void appendContextMenu(Menu* menu) override {
    menu->addChild(MenuEntry::create());

    struct LoadTrackMenuItem : MenuItem {
      YM3812Player* module;

      void onAction(EventAction& e) override {
	selectTrackFileAndPlay(module);
      }
    };

    LoadTrackMenuItem *load = MenuItem::create<LoadTrackMenuItem>("Load track", "");
    load->module = module_;
    menu->addChild(load);
  }

};

Model *modelYM3812Player = Model::create<YM3812Player, YM3812PlayerWidget>("YMod3812", "0x01Player", "YM3812 Track player", OSCILLATOR_TAG);
