#ifndef BIDISCHMITTTRIGGER_HPP
#define BIDISCHMITTTRIGGER_HPP

// A Schmitt Trigger whose process() returns true on either low or high transition
struct BidiSchmittTrigger {
  const float lo_;
  const float hi_;
  bool state;
  
  BidiSchmittTrigger(float lo = 0.1f, float hi = 1.f) : lo_(lo), hi_(hi), state(false) {}
  
  bool process(float f) {
    bool triggered = false;
    if (f > hi_) {
      if (state != true) triggered = true;
      state = true;
    } else if (f < lo_) {
      if (state != false) triggered = true;
      state = false;
    }
    return triggered;
  }
};

#endif
