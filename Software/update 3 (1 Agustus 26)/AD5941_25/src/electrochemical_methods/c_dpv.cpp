// Differential Pulse Voltammetry (DPV) implementation module.
// Applies potential pulses on top of a staircase scan and measures the current differential before and during the pulse.

#include "c_dpv.h"
#include "../ad5940/ad5940.h"
#include "../setup/ad5941_setup.h"
#include "../../utilities.h"
#include "../utils/status_utils.h"

// Access global variables from the orchestrator
extern ADCFilterCfg_Type adc_filter;
extern ADCBaseCfg_Type adc_base;
extern HSLoopCfg_Type HpLoopCfg;
extern uint8_t tia_rf;
extern uint32_t SetDACLevel(float mV);

/**
 * @brief Binds the class instance to the global parameters state storage.
 * @param pData Reference to the global C_DataStorage instance.
 */
void C_DPV::Begin(C_DataStorage* pData)
{
    m_pData = pData;
}

/**
 * @brief Configures AFE registers and matrix switches for DC potential measurement.
 * @param voltage_mV The target potential to apply.
 */
void C_DPV::ConfigDCMeasurement(float voltage_mV)
{
    // Disable Waveform Generator during simple DC potential steps
    AD5940_AFECtrlS(AFECTRL_WG, bFALSE);
    
    // Set target potential
    WGCfg_Type wgInit;
    wgInit.WgType = WGTYPE_MMR;
    wgInit.GainCalEn = bTRUE;
    wgInit.OffsetCalEn = bFALSE;
    wgInit.WgCode = SetDACLevel(voltage_mV);
    AD5940_WGCfgS(&wgInit);

    // Switch Matrix mappings: Route pins for Counter, Reference and Working electrodes
    HpLoopCfg.SWMatCfg.Dswitch = SWD_CE0;
    HpLoopCfg.SWMatCfg.Pswitch = SWP_RE0;
    HpLoopCfg.SWMatCfg.Nswitch = SWN_SE0;
    HpLoopCfg.SWMatCfg.Tswitch = SWT_TRTIA | SWT_SE0LOAD;

    // HSTIA configuration - without this the transimpedance amplifier feedback
    // path is left at whatever HpLoopCfg last had (zeroed at boot), so current
    // measurements silently read zero regardless of the actual cell current.
    HpLoopCfg.HsTiaCfg.DiodeClose = bFALSE;
    HpLoopCfg.HsTiaCfg.HstiaBias = HSTIABIAS_1P1;
    HpLoopCfg.HsTiaCfg.HstiaCtia = 16; /* 16pF capacitor */
    HpLoopCfg.HsTiaCfg.HstiaDeRload = HSTIADERLOAD_OPEN;
    HpLoopCfg.HsTiaCfg.HstiaDeRtia = HSTIADERTIA_TODE; /* Connect HSTIA output to DE0 pin */
    HpLoopCfg.HsTiaCfg.HstiaRtiaSel = (tia_rf < 8) ? tia_rf : HSTIARTIA_10K; /* Keep in sync with RawToCurrent()'s rf_values[] */
    AD5940_HSLoopCfgS(&HpLoopCfg);

    // Enable power to essential amplifiers
    AD5940_AFECtrlS(AFECTRL_DACREFPWR | AFECTRL_EXTBUFPWR | AFECTRL_INAMPPWR |
                    AFECTRL_HSTIAPWR | AFECTRL_HSDACPWR | AFECTRL_DCBUFPWR, bTRUE);
                    
    // Map ADC Multiplexer to TIA output nodes
    AD5940_ADCMuxCfgS(ADCMUXP_HSTIA_P, ADCMUXN_HSTIA_N);

    // PGA gain set to 1x
    adc_base.ADCPga = 1;
    AD5940_ADCBaseCfgS(&adc_base);

    // Filter chain: Sinc3 OSR = 4, Sinc2 OSR = 1333, average 16 readings
    adc_filter.ADCSinc3Osr = ADCSINC3OSR_4;
    adc_filter.ADCSinc2Osr = ADCSINC2OSR_1333;
    adc_filter.ADCAvgNum = ADCAVGNUM_16;
    adc_filter.ADCRate = ADCRATE_800KHZ;
    adc_filter.BpNotch = bTRUE;
    adc_filter.BpSinc3 = bFALSE;
    adc_filter.Sinc2NotchEnable = bTRUE;
    AD5940_ADCFilterCfgS(&adc_filter);

    // Route the SINC2-ready source into INTC1 - without this,
    // AD5940_TakeMeasurement()'s ready-flag poll in MeasureCurrentRaw() can
    // never see a true flag and always times out returning a stale 0.
    AD5940_INTCCfg(AFEINTC_1, AFEINTSRC_ALLINT, bTRUE);
    AD5940_INTCClrFlag(AFEINTSRC_ALLINT);
}

/**
 * @brief Triggers a single ADC conversion cycle.
 * @return Raw output code from the Sinc2 filter.
 */
uint32_t C_DPV::MeasureCurrentRaw()
{
    AD5940_AFECtrlS(AFECTRL_ADCPWR | AFECTRL_ADCCNV, bTRUE);
    delayMicroseconds(500);
    
    int32_t time_out = 1000;
    uint32_t result = AD5940_TakeMeasurement(AFECTRL_ADCPWR | AFECTRL_ADCCNV,
                                             AFEINTSRC_SINC2RDY,
                                             AFERESULT_SINC2,
                                             &time_out);
                                             
    AD5940_AFECtrlS(AFECTRL_ADCCNV, bFALSE);
    return result;
}

/**
 * @brief Resolves raw ADC counts into cell current in Amperes.
 * @param rawCode Raw 16-bit output code.
 * @return Current value in Amperes.
 */
float C_DPV::RawToCurrent(uint32_t rawCode)
{
    const float Vref_mV = 1820.0f;
    float PGA_G = 1.0f;
    float Rf_Ohm = 200.0f;
    
    uint32_t rf_values[] = {200, 1000, 5000, 10000, 20000, 40000, 80000, 160000};
    if (tia_rf < 8) Rf_Ohm = rf_values[tia_rf];
    
    float code = (int16_t)(rawCode & 0xFFFF);
    return (code * Vref_mV / 1000.0f) / (PGA_G * Rf_Ohm * 32768.0f);
}

/**
 * @brief Executes the complete Differential Pulse Voltammetry (DPV) sweep sequence and streams results to Serial.
 */
void C_DPV::Run()
{
    Utils_SetStatusLed(MAGENTA); // MAGENTA signals DPV sweep is active
    Serial.println("DPV_START");

    // Calculate total staircase step count
    int numSteps = (int)(fabs(m_pData->DPV_End_mV - m_pData->DPV_Start_mV) / m_pData->DPV_Step_mV) + 1;
    float voltage = m_pData->DPV_Start_mV;
    float stepDirection = (m_pData->DPV_End_mV > m_pData->DPV_Start_mV) ? m_pData->DPV_Step_mV : -m_pData->DPV_Step_mV;
    
    // Set pulse delays
    int pulseDelay_us = (int)(m_pData->DPV_PulseWidth_s * 1e6f);
    int periodDelay_us = (int)(m_pData->DPV_PulsePeriod_s * 1e6f);
    int sampleDelay_us = (int)(m_pData->DPV_SampleDelay_s * 1e6f); // Wait time before trigger conversion

    for (int step = 0; step < numSteps; ++step)
    {
        // 1. Measure current at base staircase potential (I_base)
        ConfigDCMeasurement(voltage);
        delayMicroseconds(sampleDelay_us);
        uint32_t raw_base = MeasureCurrentRaw();
        float I_base = RawToCurrent(raw_base);

        // 2. Measure current during pulse potential (I_pulse)
        float V_pulse = voltage + m_pData->DPV_Amplitude_mV;
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
