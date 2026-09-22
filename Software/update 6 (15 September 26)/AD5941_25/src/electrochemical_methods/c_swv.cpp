// Square Wave Voltammetry (SWV) implementation module.
// Applies a staircase potential scan overlaid with symmetrical square pulses to record differential current.

#include "c_swv.h"
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
void C_SWV::Begin(C_DataStorage* pData)
{
    m_pData = pData;
}

/**
 * @brief Configures AFE registers and matrix switches for DC potential measurement.
 *
 * Uses the AD5940 Low-Power loop (LPDAC + LPTIA), matching Analog Devices'
 * own SqrWaveVoltammetry.c reference example, instead of the High-Speed
 * loop (HSDAC/HSTIA) which is the wrong signal path for DC/staircase
 * potential steps - see c_ca.cpp for the full root-cause writeup.
 *
 * @param voltage_mV The target potential to apply between WE (SE0) and RE (RE0).
 */
void C_SWV::ConfigDCMeasurement(float voltage_mV)
{
    // The CV path ends every sweep with AD5940_ShutDownS(), which unlocks the
    // sleep key, turns the LP loop off and hibernates the AFE. Register writes
    // still succeed over SPI in that state, so configuration appeared to work
    // while the analog front end stayed powered down and the LPTIA only ever
    // returned its own offset. Wake the part and hold it awake for the whole
    // measurement, exactly as rampTest.cpp does.
    if (AD5940_WakeUp(10) > 10)
    {
        Serial.println("ERR,AFE_WAKEUP_FAILED");
        return;
    }
    AD5940_SleepKeyCtrlS(SLPKEY_LOCK);   // no hibernation mid-measurement

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
    // Cell potential is V(WE) - V(RE) = Vzero - Vbias, so a positive applied
    // potential needs Vbias *below* Vzero. Adding the offset inverted the
    // polarity of every CA/SWV/DPV measurement. rampTest.cpp subtracts.
    int32_t vbiasCode = (int32_t)(vzeroCode * 64) - (int32_t)(voltage_mV / DAC12BITVOLT_1LSB);
    if (vbiasCode > 4095) vbiasCode = 4095;
    if (vbiasCode < 0) vbiasCode = 0;
    if (vzeroCode > 63) vzeroCode = 63;

    uint32_t lpRtiaCode = (tia_rf < 8) ? kTiaRfToLpRtiaCode[tia_rf] : LPTIARTIA_10K;

    LPLoopCfg_Type lp_loop;
    lp_loop.LpDacCfg.LpdacSel = LPDAC0;
    lp_loop.LpDacCfg.LpDacSrc = LPDACSRC_MMR;
    // LPDACSW_VZERO2PIN ties the Vzero buffer straight to the SE0 working-
    // electrode pin. With it closed the cell current is sunk by that
    // low-impedance buffer instead of flowing through RTIA into the TIA
    // summing node, so the LPTIA only ever saw its own input leakage
    // (~55 nA) no matter what potential was applied. The working CV path
    // (rampTest.cpp) leaves it open; do the same.
    lp_loop.LpDacCfg.LpDacSW = LPDACSW_VBIAS2LPPA | LPDACSW_VZERO2LPTIA;
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
    // LPTIARF_1M + SW13 routes the LPTIA output to the ADC through a 1 MOhm
    // filter resistor, which on ADI's eval board works against an external
    // filter capacitor on the LPF pin. This carrier board does not fit that
    // capacitor, so the 1 MOhm sits in series with the ADC's switched-cap
    // input and attenuates the signal by ~2 orders of magnitude - the cell
    // current was being read as a few tens of nA regardless of applied
    // potential. Bypass the filter and take the LPTIA output directly, which
    // is what the working CV path (rampTest.cpp) already does.
    extern uint8_t lptia_topology;
    lp_loop.LpAmpCfg.LpTiaRf = (uint32_t)((lptia_topology >> 1) & 0x07);
    lp_loop.LpAmpCfg.LpTiaRload = LPTIARLOAD_SHORT;
    lp_loop.LpAmpCfg.LpTiaRtia = lpRtiaCode;
    lp_loop.LpAmpCfg.LpTiaSW = LPTIASW(5) | LPTIASW(2) | LPTIASW(4)
                             | ((lptia_topology & 0x01) ? LPTIASW(13) : 0);
    AD5940_LPLoopCfgS(&lp_loop);

    AD5940_ADCMuxCfgS(ADCMUXP_LPTIA0_P, ADCMUXN_LPTIA0_N);

    // ADCPGA_1 == 0 and ADCPGA_1P5 == 1: writing a literal 1 here selected a
    // PGA gain of 1.5 while RawToCurrent() divided by 1.0, a silent 1.5x
    // current error. Name the enum so the two cannot drift apart again.
    adc_base.ADCPga = ADCPGA_1;
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
uint32_t C_SWV::MeasureCurrentRaw()
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
float C_SWV::RawToCurrent(uint32_t rawCode)
{
    const float Vref_mV = 1820.0f;
    float PGA_G = 1.0f;   // must match ADCPGA_1 set in ConfigDCMeasurement()
    float Rtia_Ohm = (tia_rf < 8) ? kTiaRfToLpRtiaOhm[tia_rf] : 10000.0f;

    // See c_ca.cpp::RawToCurrent() - zero-point is midscale (0x8000), not 0.
    float code = (float)((int32_t)rawCode - 32768);
    return (code * Vref_mV / 1000.0f) / (PGA_G * Rtia_Ohm * 32768.0f);
}

/**
 * @brief Executes the complete Square Wave Voltammetry (SWV) sweep sequence and streams results to Serial.
 */
void C_SWV::Run()
{
    Utils_SetStatusLed(YELLOW); // YELLOW signals SWV sweep is active
    Serial.println("SWV_START");

    // Calculate total staircase step count
    int numSteps = (int)(fabs(m_pData->SWV_End_mV - m_pData->SWV_Start_mV) / m_pData->SWV_Step_mV) + 1;
    float voltage = m_pData->SWV_Start_mV;
    float stepDirection = (m_pData->SWV_End_mV > m_pData->SWV_Start_mV) ? m_pData->SWV_Step_mV : -m_pData->SWV_Step_mV;
    
    // Half period represents duration of each pulse phase (forward vs. reverse)
    int halfPeriod_us = (int)(500000.0f / m_pData->SWV_Frequency_Hz);
    int sampleDelay_us = (int)(m_pData->SWV_SampleDelay_s * 1e6f); // Wait time before trigger conversion

    for (int step = 0; step < numSteps; ++step)
    {
        // 1. Forward Pulse: step potential + amplitude
        float V_forward = voltage + m_pData->SWV_Amplitude_mV;
        ConfigDCMeasurement(V_forward);
        delayMicroseconds(sampleDelay_us);
        uint32_t raw_forward = MeasureCurrentRaw();
        float I_forward = RawToCurrent(raw_forward);

        // 2. Reverse Pulse: step potential - amplitude
        float V_reverse = voltage - m_pData->SWV_Amplitude_mV;
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
