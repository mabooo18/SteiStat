#ifndef HUNSTAT_WAVE_GEN_H
#define HUNSTAT_WAVE_GEN_H

#include <Arduino.h>
#include "../../AD5940.h"

void Hardware_Do_WaveGen(uint8_t mode, float frequency, uint16_t amplitude, uint8_t tia_code);

inline void Do_WaveGen(uint8_t mode, float frequency, uint16_t amplitude, uint8_t tia_code)
{
	Hardware_Do_WaveGen(mode, frequency, amplitude, tia_code);
}

#endif
