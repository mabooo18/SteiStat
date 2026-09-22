#ifndef MEASUREMENT_BUFFER_H
#define MEASUREMENT_BUFFER_H

#include <Arduino.h>

extern float Measurements[];
extern float* pMeasurement;
extern int numberOfMeasurements;

void ResetMeasurementBuffer();
void AppendMeasurement(float real, float imag);

#endif
