#ifndef HUNSTAT_LED_INTERFACE_H
#define HUNSTAT_LED_INTERFACE_H

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>

extern Adafruit_NeoPixel pixels;

void Interface_SetLed(uint8_t color);
void Interface_SetPixelsColor(uint8_t red, uint8_t green, uint8_t blue);

#endif
