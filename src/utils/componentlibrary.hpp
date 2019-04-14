#ifndef COMPONENTLIBRARY_HPP
#define COMPONENTLIBRARY_HPP

// Makes any knob snappy
template<typename Base>
struct SnappyKnob : Base {
  SnappyKnob() {
    this->snap = true;
  }
};

// Adds a border around a LED light
template<typename Base>
struct BorderLEDLight : Base {
  BorderLEDLight() {
    this->borderColor = nvgRGB(0, 0, 0);
    this->bgColor = nvgRGBA(80, 80, 80, 0);
  }
};

#endif
