// Procedural implementation wrappers for CA, SWV, and DPV.
// These functions act as backward-compatibility entry points for running electrochemical measurements.

#include "electrochemical_methods.h"
#include <math.h>
#include "../ad5940/debug.h"
#include "../ad5940/ad5940.h"
#include "../../utilities.h"
#include "../utils/status_utils.h"

// --- Global variables for CA/SWV/DPV procedural states ---
float CA_Voltage_mV = 0.0;
float CA_Duration_s = 1.0;
float CA_SampleRate_Hz = 100.0;
uint32_t CA_NumSamples = 0;

float SWV_Start_mV = -100.0;
float SWV_End_mV = 100.0;
float SWV_Step_mV = 5.0;
float SWV_Amplitude_mV = 25.0;
float SWV_Frequency_Hz = 50.0;
float SWV_CurrentSampleDelay_s = 0.02;

float DPV_Start_mV = -100.0;
float DPV_End_mV = 100.0;
float DPV_Step_mV = 5.0;
float DPV_Amplitude_mV = 50.0;
float DPV_PulseWidth_s = 0.05;
float DPV_PulsePeriod_s = 0.2;
float DPV_CurrentSampleDelay_s = 0.02;

// Access global hardware configurations from the main program
extern ADCFilterCfg_Type adc_filter;
extern ADCBaseCfg_Type adc_base;
extern HSLoopCfg_Type HpLoopCfg;
extern uint8_t pga_gain;
extern uint8_t tia_rf;
extern uint8_t EIS_mode;
extern uint32_t SetDACLevel(float mV);

/**
 * @brief Configures AFE registers and matrix switches for DC potential measurement.
 * @param voltage_mV The target potential to apply across WE and RE.
 */
void Config_AD5941_DCMeasurement(float voltage_mV)
{
    // Disable high speed wave generators to write target levels directly
    AD5940_AFECtrlS(AFECTRL_WG, bFALSE);

    WGCfg_Type wgInit;
    wgInit.WgType = WGTYPE_MMR;
    wgInit.GainCalEn = bTRUE;
    wgInit.OffsetCalEn = bFALSE;

    uint32_t hsDacDat = SetDACLevel(voltage_mV);
    wgInit.WgCode = hsDacDat;
    AD5940_WGCfgS(&wgInit);

    // Switch Matrix mappings: Route pins for Counter, Reference and Working electrodes
    HpLoopCfg.SWMatCfg.Dswitch = SWD_CE0;
    HpLoopCfg.SWMatCfg.Pswitch = SWP_RE0;
    HpLoopCfg.SWMatCfg.Nswitch = SWN_SE0;
    HpLoopCfg.SWMatCfg.Tswitch = SWT_TRTIA | SWT_SE0LOAD;
    AD5940_HSLoopCfgS(&HpLoopCfg);

    // Enable power to essential amplifiers
    AD5940_AFECtrlS(AFECTRL_DACREFPWR | AFECTRL_EXTBUFPWR | AFECTRL_INAMPPWR |
                    AFECTRL_HSTIAPWR | AFECTRL_HSDACPWR | AFECTRL_DCBUFPWR, bTRUE);

    // Map ADC Multiplexer to TIA output nodes
    AD5940_ADCMuxCfgS(ADCMUXP_HSTIA_P, ADCMUXN_HSTIA_N);

    // Set PGA gain to 1x
    adc_base.ADCPga = 1;
    AD5940_ADCBaseCfgS(&adc_base);

    // Filter chain: SINC3 OSR = 4, SINC2 OSR = 1333, average 16 readings
    adc_filter.ADCSinc3Osr = ADCSINC3OSR_4;
    adc_filter.ADCSinc2Osr = ADCSINC2OSR_1333;
    adc_filter.ADCAvgNum = ADCAVGNUM_16;
    adc_filter.ADCRate = ADCRATE_800KHZ;
    adc_filter.BpNotch = bTRUE;
    adc_filter.BpSinc3 = bFALSE;
    adc_filter.Sinc2NotchEnable = bTRUE;
    AD5940_ADCFilterCfgS(&adc_filter);
}

/**
 * @brief Triggers a single ADC conversion cycle to measure current.
 * @return Raw output code from the Sinc2 filter.
 */
uint32_t MeasureCurrentRaw()
{
    AD5940_AFECtrlS(AFECTRL_ADCPWR | AFECTRL_ADCCNV, bTRUE);
    delayMicroseconds(500);

    int32_t time_out = 100;
    uint32_t result = AD5940_TakeMeasurement(AFECTRL_ADCPWR | AFECTRL_ADCCNV,
                                             AFEINTSRC_SINC2RDY,
                                             AFERESULT_SINC2, &time_out);

    AD5940_AFECtrlS(AFECTRL_ADCCNV, bFALSE);
    return result;
}

/**
 * @brief Resolves raw ADC counts into cell current in Amperes.
 * @param rawCode Raw 16-bit output code.
 * @return Current value in Amperes.
 */
float RawToCurrent(uint32_t rawCode)
{
    const float Vref_mV = 1820.0;
    float PGA_G = 1.0;
    float Rf_Ohm = 200.0;

    // Look up TIA resistor value
    uint32_t rf_values[] = {200, 1000, 5000, 10000, 20000, 40000, 80000, 160000};
    if (tia_rf < 8) Rf_Ohm = rf_values[tia_rf];

    float code = (int16_t)(rawCode & 0xFFFF);
    float current_A = (code * Vref_mV / 1000.0) / (PGA_G * Rf_Ohm * 32768.0);
    return current_A;
}

/**
 * @brief Procedural implementation of Chronoamperometry (CA).
 */
void RunCA()
{
    Utils_SetStatusLed(CYAN);
    Serial.println("CA_START");

    CA_NumSamples = (uint32_t)(CA_Duration_s * CA_SampleRate_Hz);
    if (CA_NumSamples < 1) CA_NumSamples = 1;

    Config_AD5941_DCMeasurement(CA_Voltage_mV);

    for (uint32_t i = 0; i < CA_NumSamples; i++)
    {
        uint32_t raw = MeasureCurrentRaw();
        float current_A = RawToCurrent(raw);
        float time_s = (float)i / CA_SampleRate_Hz;

        char buf[100];
        snprintf(buf, sizeof(buf), "CA,%.4f,%.4e", time_s, current_A);
        Serial.println(buf);

        delay((int)(1000.0 / CA_SampleRate_Hz));
    }

    AD5940_AFECtrlS(AFECTRL_ADCPWR | AFECTRL_ADCCNV | AFECTRL_WG | AFECTRL_DACREFPWR, bFALSE);
    Utils_SetStatusLed(GREEN);
    Serial.println("CA_END");
}

/**
 * @brief Procedural implementation of Square Wave Voltammetry (SWV).
 */
void RunSWV()
{
    Utils_SetStatusLed(YELLOW);
    Serial.println("SWV_START");

    int numSteps = (int)(fabs(SWV_End_mV - SWV_Start_mV) / SWV_Step_mV) + 1;
    float voltage = SWV_Start_mV;
    float stepDirection = (SWV_End_mV > SWV_Start_mV) ? SWV_Step_mV : -SWV_Step_mV;

    int halfPeriod_us = (int)(500000.0 / SWV_Frequency_Hz);
    int sampleDelay_us = (int)(SWV_CurrentSampleDelay_s * 1e6);

    for (int step = 0; step < numSteps; step++)
    {
        // Forward pulse phase
        float V_forward = voltage + SWV_Amplitude_mV;
        Config_AD5941_DCMeasurement(V_forward);
        delayMicroseconds(sampleDelay_us);
        uint32_t raw_forward = MeasureCurrentRaw();
        float I_forward = RawToCurrent(raw_forward);

        // Reverse pulse phase
        float V_reverse = voltage - SWV_Amplitude_mV;
        Config_AD5941_DCMeasurement(V_reverse);
        delayMicroseconds(sampleDelay_us);
        uint32_t raw_reverse = MeasureCurrentRaw();
        float I_reverse = RawToCurrent(raw_reverse);

        float delta_I = I_forward - I_reverse;

        char buf[100];
        snprintf(buf, sizeof(buf), "SWV,%.2f,%.4e", voltage, delta_I);
        Serial.println(buf);

        voltage += stepDirection;
        delayMicroseconds(halfPeriod_us * 2 - sampleDelay_us * 2);
    }

    AD5940_AFECtrlS(AFECTRL_ADCPWR | AFECTRL_ADCCNV | AFECTRL_WG | AFECTRL_DACREFPWR, bFALSE);
    Utils_SetStatusLed(GREEN);
    Serial.println("SWV_END");
}

/**
 * @brief Procedural implementation of Differential Pulse Voltammetry (DPV).
 */
void RunDPV()
{
    Utils_SetStatusLed(MAGENTA);
    Serial.println("DPV_START");

    int numSteps = (int)(fabs(DPV_End_mV - DPV_Start_mV) / DPV_Step_mV) + 1;
    float voltage = DPV_Start_mV;
    float stepDirection = (DPV_End_mV > DPV_Start_mV) ? DPV_Step_mV : -DPV_Step_mV;

    int pulseDelay_us = (int)(DPV_PulseWidth_s * 1e6);
    int periodDelay_us = (int)(DPV_PulsePeriod_s * 1e6);
    int sampleDelay_us = (int)(DPV_CurrentSampleDelay_s * 1e6);

    for (int step = 0; step < numSteps; step++)
    {
        // Baseline phase
        Config_AD5941_DCMeasurement(voltage);
        delayMicroseconds(sampleDelay_us);
        uint32_t raw_base = MeasureCurrentRaw();
        float I_base = RawToCurrent(raw_base);

        // Pulse phase
        float V_pulse = voltage + DPV_Amplitude_mV;
        Config_AD5941_DCMeasurement(V_pulse);
        delayMicroseconds(sampleDelay_us);
        uint32_t raw_pulse = MeasureCurrentRaw();
        float I_pulse = RawToCurrent(raw_pulse);

        float delta_I = I_pulse - I_base;

        char buf[100];
        snprintf(buf, sizeof(buf), "DPV,%.2f,%.4e", voltage, delta_I);
        Serial.println(buf);

        voltage += stepDirection;
        delayMicroseconds(periodDelay_us - pulseDelay_us);
    }

    AD5940_AFECtrlS(AFECTRL_ADCPWR | AFECTRL_ADCCNV | AFECTRL_WG | AFECTRL_DACREFPWR, bFALSE);
    Utils_SetStatusLed(GREEN);
    Serial.println("DPV_END");
}
