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

// DAC LSB sizes for the AD5940 LPDAC (12-bit Vbias / 6-bit Vzero channels),
// per AD5940/AD5941.pdf and the ADI ChronoAmperometric.c reference example.
static const float DAC12BITVOLT_1LSB = 2200.0f / 4095;
static const float DAC6BITVOLT_1LSB  = DAC12BITVOLT_1LSB * 64;

// tia_rf (0..7, set via the 'r' command) maps to LPTIA internal RTIA codes.
// See c_ca.cpp for the derivation of this table.
static const uint32_t kTiaRfToLpRtiaCode[8] = {
    LPTIARTIA_200R, LPTIARTIA_1K, LPTIARTIA_4K,  LPTIARTIA_10K,
    LPTIARTIA_20K,  LPTIARTIA_40K, LPTIARTIA_85K, LPTIARTIA_160K
};
static const float kTiaRfToLpRtiaOhm[8] = {
    110.0f, 1000.0f, 4000.0f, 10000.0f, 20000.0f, 40000.0f, 85000.0f, 160000.0f
};

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
 *
 * Uses the AD5940 Low-Power loop (LPDAC + LPTIA); see c_ca.cpp for the full
 * root-cause writeup of why the previous High-Speed loop (HSDAC/HSTIA)
 * path was wrong for DC/staircase potential steps.
 *
 * @param voltage_mV The target potential to apply between WE (SE0) and RE (RE0).
 */
void C_DPV::ConfigDCMeasurement(float voltage_mV)
{
    AFERefCfg_Type aferef_cfg;
    aferef_cfg.HpBandgapEn = bTRUE;
    aferef_cfg.Hp1V1BuffEn = bTRUE;
    aferef_cfg.Hp1V8BuffEn = bTRUE;
    aferef_cfg.Disc1V1Cap = bFALSE;
    aferef_cfg.Disc1V8Cap = bFALSE;
    aferef_cfg.Hp1V8ThemBuff = bFALSE;
    aferef_cfg.Hp1V8Ilimit = bFALSE;
    aferef_cfg.Lp1V1BuffEn = bTRUE;
    aferef_cfg.Lp1V8BuffEn = bTRUE;
    aferef_cfg.LpBandgapEn = bTRUE;
    aferef_cfg.LpRefBufEn = bTRUE;
    aferef_cfg.LpRefBoostEn = bFALSE;
    AD5940_REFCfgS(&aferef_cfg);

    HSLoopCfg_Type hs_loop;
    AD5940_StructInit(&hs_loop, sizeof(hs_loop));
    AD5940_HSLoopCfgS(&hs_loop);
    AD5940_AFECtrlS(AFECTRL_WG | AFECTRL_HSTIAPWR | AFECTRL_HSDACPWR, bFALSE);

    const float Vzero_mV = 1100.0f;
    uint32_t vzeroCode = (uint32_t)((Vzero_mV - 200.0f) / DAC6BITVOLT_1LSB);
    int32_t vbiasCode = (int32_t)(voltage_mV / DAC12BITVOLT_1LSB) + (int32_t)(vzeroCode * 64);
    if (vbiasCode < (int32_t)(vzeroCode * 64)) vbiasCode--;
    if (vbiasCode > 4095) vbiasCode = 4095;
    if (vbiasCode < 0) vbiasCode = 0;
    if (vzeroCode > 63) vzeroCode = 63;

    uint32_t lpRtiaCode = (tia_rf < 8) ? kTiaRfToLpRtiaCode[tia_rf] : LPTIARTIA_10K;

    LPLoopCfg_Type lp_loop;
    lp_loop.LpDacCfg.LpdacSel = LPDAC0;
    lp_loop.LpDacCfg.LpDacSrc = LPDACSRC_MMR;
    lp_loop.LpDacCfg.LpDacSW = LPDACSW_VBIAS2LPPA | LPDACSW_VZERO2LPTIA | LPDACSW_VZERO2PIN;
    lp_loop.LpDacCfg.LpDacVzeroMux = LPDACVZERO_6BIT;
    lp_loop.LpDacCfg.LpDacVbiasMux = LPDACVBIAS_12BIT;
    lp_loop.LpDacCfg.LpDacRef = LPDACREF_2P5;
    lp_loop.LpDacCfg.DataRst = bFALSE;
    lp_loop.LpDacCfg.PowerEn = bTRUE;
    lp_loop.LpDacCfg.DacData6Bit = vzeroCode;
    lp_loop.LpDacCfg.DacData12Bit = (uint32_t)vbiasCode;

    lp_loop.LpAmpCfg.LpAmpSel = LPAMP0;
    lp_loop.LpAmpCfg.LpAmpPwrMod = LPAMPPWR_NORM;
    lp_loop.LpAmpCfg.LpPaPwrEn = bTRUE;
    lp_loop.LpAmpCfg.LpTiaPwrEn = bTRUE;
    lp_loop.LpAmpCfg.LpTiaRf = LPTIARF_1M;
    lp_loop.LpAmpCfg.LpTiaRload = LPTIARLOAD_SHORT;
    lp_loop.LpAmpCfg.LpTiaRtia = lpRtiaCode;
    lp_loop.LpAmpCfg.LpTiaSW = LPTIASW(5) | LPTIASW(2) | LPTIASW(4) | LPTIASW(13);
    AD5940_LPLoopCfgS(&lp_loop);

    AD5940_ADCMuxCfgS(ADCMUXP_LPTIA0_P, ADCMUXN_LPTIA0_N);

    adc_base.ADCPga = 1;
    AD5940_ADCBaseCfgS(&adc_base);

    adc_filter.ADCSinc3Osr = ADCSINC3OSR_4;
    adc_filter.ADCSinc2Osr = ADCSINC2OSR_1333;
    adc_filter.ADCAvgNum = ADCAVGNUM_16;
    adc_filter.ADCRate = ADCRATE_800KHZ;
    adc_filter.BpNotch = bTRUE;
    adc_filter.BpSinc3 = bFALSE;
    adc_filter.Sinc2NotchEnable = bTRUE;
    AD5940_ADCFilterCfgS(&adc_filter);

    AD5940_AFECtrlS(AFECTRL_HPREFPWR | AFECTRL_SINC2NOTCH, bTRUE);

    AD5940_INTCCfg(AFEINTC_1, AFEINTSRC_ALLINT, bTRUE);
    AD5940_INTCClrFlag(AFEINTSRC_ALLINT);
}

/**
 * @brief Triggers a single fresh ADC conversion cycle to measure current.
 * @return Raw output code from the Sinc2 filter.
 */
uint32_t C_DPV::MeasureCurrentRaw()
{
    AD5940_AFECtrlS(AFECTRL_ADCPWR, bTRUE);
    delayMicroseconds(250); // ADC power-up settle, matches ADI's WAIT(16*250) at 16MHz

    // See c_ca.cpp::MeasureCurrentRaw() for why this needs ~13.3ms of
    // headroom rather than the previous 500us/10ms budget.
    int32_t time_out = 2500;
    uint32_t result = AD5940_TakeMeasurement(AFECTRL_ADCCNV,
                                             AFEINTSRC_SINC2RDY,
                                             AFERESULT_SINC2,
                                             &time_out);
    AD5940_AFECtrlS(AFECTRL_ADCPWR, bFALSE);
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
    float Rtia_Ohm = (tia_rf < 8) ? kTiaRfToLpRtiaOhm[tia_rf] : 10000.0f;

    // See c_ca.cpp::RawToCurrent() - zero-point is midscale (0x8000), not 0.
    float code = (float)((int32_t)rawCode - 32768);
    return (code * Vref_mV / 1000.0f) / (PGA_G * Rtia_Ohm * 32768.0f);
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
