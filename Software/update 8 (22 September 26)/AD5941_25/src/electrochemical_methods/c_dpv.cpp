// Differential Pulse Voltammetry (DPV) implementation module.
// Applies potential pulses on top of a staircase scan and measures the current differential before and during the pulse.
// AFE configuration / sampling / conversion are inherited from C_DCPotentiostatMethod
// (dc_potentiostat_method.h/.cpp); this file only implements the DPV-specific Run() sequence.

#include "c_dpv.h"
#include "../ad5940/ad5940.h"
#include "../../utilities.h"
#include "../utils/status_utils.h"

/**
 * @brief Executes the complete Differential Pulse Voltammetry (DPV) sweep sequence and streams results to Serial.
 */
void C_DPV::Run()
{
    Utils_SetStatusLed(MAGENTA); // MAGENTA signals DPV sweep is active
    Serial.println("DPV_START");

    // Calculate total staircase step count
    int numSteps = (int)(fabs(m_pData->GetDPV_End_mV() - m_pData->GetDPV_Start_mV()) / m_pData->GetDPV_Step_mV()) + 1;
    float voltage = m_pData->GetDPV_Start_mV();
    float stepDirection = (m_pData->GetDPV_End_mV() > m_pData->GetDPV_Start_mV()) ? m_pData->GetDPV_Step_mV() : -m_pData->GetDPV_Step_mV();

    // Set pulse delays
    int pulseDelay_us = (int)(m_pData->GetDPV_PulseWidth_s() * 1e6f);
    int periodDelay_us = (int)(m_pData->GetDPV_PulsePeriod_s() * 1e6f);
    int sampleDelay_us = (int)(m_pData->GetDPV_SampleDelay_s() * 1e6f); // Wait time before trigger conversion

    for (int step = 0; step < numSteps; ++step)
    {
        // 1. Measure current at base staircase potential (I_base)
        ConfigDCMeasurement(voltage);
        delayMicroseconds(sampleDelay_us);
        uint32_t raw_base = MeasureCurrentRaw();
        float I_base = RawToCurrent(raw_base);

        // 2. Measure current during pulse potential (I_pulse)
        float V_pulse = voltage + m_pData->GetDPV_Amplitude_mV();
        ConfigDCMeasurement(V_pulse);
        delayMicroseconds(sampleDelay_us);
        uint32_t raw_pulse = MeasureCurrentRaw();
        float I_pulse = RawToCurrent(raw_pulse);

        // Calculate differential current: I_pulse - I_base
        float delta_I = I_pulse - I_base;

        // Format: DPV,<staircase_voltage_mV>,<differential_current_Amps>
        char buf[100];
        snprintf(buf, sizeof(buf), "DPV,%.2f,%.4e", voltage, delta_I);
        Serial.println(buf);

        // Progress step potential
        voltage += stepDirection;

        // Wait to complete the step period duration
        delayMicroseconds(periodDelay_us - pulseDelay_us);
    }

    // Power down AFE components
    AD5940_AFECtrlS(AFECTRL_ADCPWR | AFECTRL_ADCCNV | AFECTRL_WG | AFECTRL_DACREFPWR, bFALSE);

    Utils_SetStatusLed(GREEN); // GREEN indicates finished
    Serial.println("DPV_END");
}
