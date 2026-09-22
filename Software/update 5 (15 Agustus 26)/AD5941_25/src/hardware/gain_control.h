#ifndef HUNSTAT_GAIN_CONTROL_H
#define HUNSTAT_GAIN_CONTROL_H

#include <Arduino.h>

void Hardware_FindOptimum_Rf_PGA(uint32_t CGmax, uint32_t CGmin, float freq, uint8_t* TIA_Rf, uint8_t* PGA_gain, float* closest_CG);

#endif
