#ifndef ADAFRUIT_NEOPIXEL_H
#define ADAFRUIT_NEOPIXEL_H

#include "stdint.h"

#define NEO_GRB 0x00
#define NEO_KHZ800 0x00

typedef uint8_t neoPixelType;

class Adafruit_NeoPixel {
public:
    Adafruit_NeoPixel(uint16_t n = 1, int16_t p = 6, neoPixelType t = NEO_GRB + NEO_KHZ800)
        : _numPixels(n), _pin(p), _type(t), _pixelColor(0) {}

    void begin() {}
    void clear() { _pixelColor = 0; }
    void setPixelColor(uint16_t n, uint32_t c) {
        if (n < _numPixels) {
            _pixelColor = c;
        }
    }
    void show() {}
    void setBrightness(uint8_t) {}
    uint32_t getPixelColor(uint16_t n) const {
        return (n < _numPixels) ? _pixelColor : 0;
    }
    uint32_t Color(uint8_t r, uint8_t g, uint8_t b) const {
        return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
    }

private:
    uint16_t _numPixels;
    int16_t _pin;
    neoPixelType _type;
    uint32_t _pixelColor;
};

#endif
