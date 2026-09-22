#ifndef ELECTROCHEMICAL_METHODS_H
#define ELECTROCHEMICAL_METHODS_H

#include <Arduino.h>

// CA/SWV/DPV parameters
extern float CA_Voltage_mV;
extern float CA_Duration_s;
extern float CA_SampleRate_Hz;
extern uint32_t CA_NumSamples;

extern float SWV_Start_mV;
extern float SWV_End_mV;
extern float SWV_Step_mV;
extern float SWV_Amplitude_mV;
extern float SWV_Frequency_Hz;
extern float SWV_CurrentSampleDelay_s;

extern float DPV_Start_mV;
extern float DPV_End_mV;
extern float DPV_Step_mV;
extern float DPV_Amplitude_mV;
extern float DPV_PulseWidth_s;
extern float DPV_PulsePeriod_s;
extern float DPV_CurrentSampleDelay_s;

void Config_AD5941_DCMeasurement(float voltage_mV);
uint32_t MeasureCurrentRaw();
float RawToCurrent(uint32_t rawCode);
void RunCA();
void RunSWV();
void RunDPV();

#endif
