#ifndef HUNSTAT_ADC_CONTROL_H
#define HUNSTAT_ADC_CONTROL_H

#include <Arduino.h>
#include "../../AD5940.h"

void Hardware_Init_AD5940_ADC(float freq);

inline void init_AD5940_ADC(float freq)
{
	Hardware_Init_AD5940_ADC(freq);
}

#endif
