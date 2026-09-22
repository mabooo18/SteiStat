// Square Wave Voltammetry (SWV) implementation module.
// Applies alternating forward/reverse potential pulses on top of a staircase scan and measures the current differential.
// AFE configuration / sampling / conversion are inherited from C_DCPotentiostatMethod
// (dc_potentiostat_method.h/.cpp); this file only implements the SWV-specific Run() sequence.

#include "c_swv.h"
#include "../ad5940/ad5940.h"
#include "../../utilities.h"
#include "../utils/status_utils.h"

/**
 * @brief Executes the complete Square Wave Voltammetry (SWV) sweep sequence and streams results to Serial.
 */
void C_SWV::Run()
{
    Utils_SetStatusLed(YELLOW); // YELLOW signals SWV sweep is active
    Serial.println("SWV_START");

    // Calculate total staircase step count
    int numSteps = (int)(fabs(m_pData->GetSWV_End_mV() - m_pData->GetSWV_Start_mV()) / m_pData->GetSWV_Step_mV()) + 1;
    float voltage = m_pData->GetSWV_Start_mV();
    float stepDirection = (m_pData->GetSWV_End_mV() > m_pData->GetSWV_Start_mV()) ? m_pData->GetSWV_Step_mV() : -m_pData->GetSWV_Step_mV();

    // Half period represents duration of each pulse phase (forward vs. reverse)
    int halfPeriod_us = (int)(500000.0f / m_pData->GetSWV_Frequency_Hz());
    int sampleDelay_us = (int)(m_pData->GetSWV_SampleDelay_s() * 1e6f); // Wait time before trigger conversion

    for (int step = 0; step < numSteps; ++step)
    {
        // 1. Forward Pulse: step potential + amplitude
        float V_forward = voltage + m_pData->GetSWV_Amplitude_mV();
        ConfigDCMeasurement(V_forward);
        delayMicroseconds(sampleDelay_us);
        uint32_t raw_forward = MeasureCurrentRaw();
        float I_forward = RawToCurrent(raw_forward);

        // 2. Reverse Pulse: step potential - amplitude
        float V_reverse = voltage - m_pData->GetSWV_Amplitude_mV();
        ConfigDCMeasurement(V_reverse);
        delayMicroseconds(sampleDelay_us);
        uint32_t raw_reverse = MeasureCurrentRaw();
        float I_reverse = RawToCurrent(raw_reverse);

        // Calculate differential current: I_forward - I_reverse
        float delta_I = I_forward - I_reverse;

        // Format: SWV,<staircase_voltage_mV>,<differential_current_Amps>
        char buf[100];
        snprintf(buf, sizeof(buf), "SWV,%.2f,%.4e", voltage, delta_I);
        Serial.println(buf);

        // Progress step potential
        voltage += stepDirection;

        // Delay to complete the current staircase period duration
        delayMicroseconds(halfPeriod_us * 2 - sampleDelay_us * 2);
    }

    // Power down AFE components
    AD5940_AFECtrlS(AFECTRL_ADCPWR | AFECTRL_ADCCNV | AFECTRL_WG | AFECTRL_DACREFPWR, bFALSE);

    Utils_SetStatusLed(GREEN); // GREEN indicates finished
    Serial.println("SWV_END");
}
