// Chronoamperometry (CA) implementation module. Steps potential to a fixed level and monitors current decay over time.
// AFE configuration / sampling / conversion are inherited from C_DCPotentiostatMethod
// (dc_potentiostat_method.h/.cpp); this file only implements the CA-specific Run() sequence.

#include "c_ca.h"
#include "../ad5940/ad5940.h"
#include "../../utilities.h"
#include "../utils/status_utils.h"

/**
 * @brief Executes the full Chronoamperometry (CA) measurement sequence and streams data to Serial.
 */
void C_CA::Run()
{
    Utils_SetStatusLed(CYAN); // CYAN indicates Chronoamperometry sweep active
    Serial.println("CA_START");

    // Calculate total steps required
    m_pData->SetCA_NumSamples((uint32_t)(m_pData->GetCA_Duration_s() * m_pData->GetCA_SampleRate_Hz()));
    if (m_pData->GetCA_NumSamples() < 1) m_pData->SetCA_NumSamples(1);

    // Load DC voltage level
    ConfigDCMeasurement(m_pData->GetCA_Voltage_mV());

    // Execute sampling loops
    for (uint32_t i = 0; i < m_pData->GetCA_NumSamples(); ++i)
    {
        uint32_t raw = MeasureCurrentRaw();
        float current_A = RawToCurrent(raw);
        float time_s = (float)i / m_pData->GetCA_SampleRate_Hz();

        // Format: CA,<time_seconds>,<current_Amps>,<raw_adc_code_debug>
        char buf[100];
        snprintf(buf, sizeof(buf), "CA,%.4f,%.4e,0x%08lX,%d", time_s, current_A, (unsigned long)raw, m_lastTimedOut ? 1 : 0);
        Serial.println(buf);

        // Block until next sampling interval
        delay((int)(1000.0f / m_pData->GetCA_SampleRate_Hz()));
    }

    // Power down converters
    AD5940_AFECtrlS(AFECTRL_ADCPWR | AFECTRL_ADCCNV | AFECTRL_WG | AFECTRL_DACREFPWR, bFALSE);

    Utils_SetStatusLed(GREEN); // GREEN indicates finished
    Serial.println("CA_END");
}
