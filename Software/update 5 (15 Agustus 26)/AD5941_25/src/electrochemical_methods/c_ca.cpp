// Chronoamperometry (CA) implementation module. Steps potential to a fixed level and monitors current decay over time.

#include "c_ca.h"
#include "../ad5940/ad5940.h"
#include "../setup/ad5941_setup.h"
#include "../../utilities.h"
#include "../utils/status_utils.h"

// Access global configuration parameters from the system orchestrator
extern ADCFilterCfg_Type adc_filter;
extern ADCBaseCfg_Type adc_base;
extern HSLoopCfg_Type HpLoopCfg;
extern uint8_t tia_rf;

// DAC LSB sizes for the AD5940 LPDAC (12-bit Vbias / 6-bit Vzero channels),
// per AD5940/AD5941.pdf and the ADI ChronoAmperometric.c reference example.
static const float DAC12BITVOLT_1LSB = 2200.0f / 4095;
static const float DAC6BITVOLT_1LSB  = DAC12BITVOLT_1LSB * 64;

// tia_rf (0..7, set via the 'r' command) maps to LPTIA internal RTIA codes.
// Chosen to keep the same command interface/defaults as the old HSTIA table
// {200,1000,5000,10000,20000,40000,80000,160000} while actually selecting a
// real LPTIA RTIA option (AD5940's factory-trimmed values differ slightly
// from nominal - see LpRtiaTable in ad5940.cpp / AD5940_LPRtiaCal).
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
void C_CA::Begin(C_DataStorage* pData)
{
    m_pData = pData;
}

/**
 * @brief Configures AFE registers and matrix switches for DC potential measurement.
 *
 * Uses the AD5940 Low-Power loop (LPDAC + LPTIA), matching Analog Devices'
 * own ChronoAmperometric.c reference example. The High-Speed loop
 * (HSDAC/HSTIA) previously used here is designed for AC/EIS excitation and
 * is the wrong signal path for a DC potential step - that mismatch is the
 * likely root cause of the flat/noisy dummy-cell results logged previously.
 *
 * @param voltage_mV The target potential to apply between WE (SE0) and RE (RE0).
 */
void C_CA::ConfigDCMeasurement(float voltage_mV)
{
    // Reference buffers: enable both HP and LP buffers as the LP loop still
    // draws from the HP bandgap for its 1.82V ADC reference.
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

    // Disable the HS loop entirely so no stale HSTIA/switch-matrix state
    // from a previous EIS/OCP run interferes with the LP loop.
    HSLoopCfg_Type hs_loop;
    AD5940_StructInit(&hs_loop, sizeof(hs_loop));
    AD5940_HSLoopCfgS(&hs_loop);
    AD5940_AFECtrlS(AFECTRL_WG | AFECTRL_HSTIAPWR | AFECTRL_HSDACPWR, bFALSE);

    // Compute LPDAC codes: Vzero fixed at mid-scale (1100 mV, matches the
    // ADI reference default), Vbias offset from Vzero by the requested
    // potential so (Vbias - Vzero) applies voltage_mV across WE/RE.
    const float Vzero_mV = 1100.0f;
    uint32_t vzeroCode = (uint32_t)((Vzero_mV - 200.0f) / DAC6BITVOLT_1LSB);
    int32_t vbiasCode = (int32_t)(voltage_mV / DAC12BITVOLT_1LSB) + (int32_t)(vzeroCode * 64);
    if (vbiasCode < (int32_t)(vzeroCode * 64)) vbiasCode--;
    if (vbiasCode > 4095) vbiasCode = 4095;
    if (vbiasCode < 0) vbiasCode = 0;
    if (vzeroCode > 63) vzeroCode = 63;

    uint32_t lpRtiaCode = (tia_rf < 8) ? kTiaRfToLpRtiaCode[tia_rf] : LPTIARTIA_10K;

    // Configure LPDAC (sets WE/RE bias) and LPTIA (senses cell current)
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

    // Configure ADC Multiplexer to measure across the LPTIA output
    AD5940_ADCMuxCfgS(ADCMUXP_LPTIA0_P, ADCMUXN_LPTIA0_N);

    // Set ADC PGA gain to 1x
    adc_base.ADCPga = 1;
    AD5940_ADCBaseCfgS(&adc_base);

    // Set ADC filters: Sinc3 OSR = 4, Sinc2 OSR = 1333, average 16 readings
    adc_filter.ADCSinc3Osr = ADCSINC3OSR_4;
    adc_filter.ADCSinc2Osr = ADCSINC2OSR_1333;
    adc_filter.ADCAvgNum = ADCAVGNUM_16;
    adc_filter.ADCRate = ADCRATE_800KHZ;
    adc_filter.BpNotch = bTRUE;
    adc_filter.BpSinc3 = bFALSE;
    adc_filter.Sinc2NotchEnable = bTRUE;
    AD5940_ADCFilterCfgS(&adc_filter);

    AD5940_AFECtrlS(AFECTRL_HPREFPWR | AFECTRL_SINC2NOTCH, bTRUE);

    // Route the SINC2-ready source into INTC1 - without this,
    // AD5940_TakeMeasurement()'s ready-flag poll in MeasureCurrentRaw() can
    // never see a true flag and always times out returning a stale 0.
    AD5940_INTCCfg(AFEINTC_1, AFEINTSRC_ALLINT, bTRUE);
    AD5940_INTCClrFlag(AFEINTSRC_ALLINT);

}

/**
 * @brief Triggers a single fresh ADC conversion cycle to measure current.
 * @note Mirrors ADI's own AppCHRONOAMPSeqMeasureGen() sequence: power up
 * ADCPWR fresh, then start ADCCNV and wait for a fully settled SINC2
 * output before powering back down.
 * @return The raw digital output code from the Sinc2 filter.
 */
uint32_t C_CA::MeasureCurrentRaw()
{
    AD5940_AFECtrlS(AFECTRL_ADCPWR, bTRUE);
    delayMicroseconds(250); // ADC power-up settle, matches ADI's WAIT(16*250) at 16MHz

    // time_out is in units of AD5940_TakeMeasurement's internal 10us poll
    // step. At ADCSinc2Osr=1333/ADCSinc3Osr=4 (RatioSys2AdcClk=1), the
    // decimation chain needs ~213555 system clocks (~13.3ms @16MHz) to
    // produce one genuinely fresh SINC2 sample (per AD5940_ClksCalculate) -
    // 2500*10us = 25ms gives comfortable margin above that. The previous
    // 500us/1000-step (10ms) budget was shorter than the true fill time,
    // so every call was very likely returning a stale/timed-out result.
    int32_t time_out = 2500;
    uint32_t result = AD5940_TakeMeasurement(AFECTRL_ADCCNV,
                                             AFEINTSRC_SINC2RDY,
                                             AFERESULT_SINC2,
                                             &time_out);
    AD5940_AFECtrlS(AFECTRL_ADCPWR, bFALSE);
    return result;
}

/**
 * @brief Resolves raw ADC bits into actual cell current in Amperes.
 * @param rawCode Raw 16-bit ADC output reading.
 * @return Current value in Amperes.
 */
float C_CA::RawToCurrent(uint32_t rawCode)
{
    const float Vref_mV = 1820.0f;          // 1.82V ADC reference voltage
    float PGA_G = 1.0f;                     // PGA gain is 1x (see ConfigDCMeasurement)

    // Actual LPTIA internal RTIA value (Ohm) for the selected tia_rf index -
    // must be kept in sync with kTiaRfToLpRtiaCode[] in ConfigDCMeasurement().
    float Rtia_Ohm = (tia_rf < 8) ? kTiaRfToLpRtiaOhm[tia_rf] : 10000.0f;

    // AD5940's signed ADC codes are offset-binary around midscale: 0x8000
    // (32768) = 0V differential, NOT 0x0000. Casting straight to int16_t
    // (as this function previously did) puts the zero-crossing at 0x0000
    // instead, which turns small real signals near 32768 into huge,
    // discontinuous swings whenever the raw code crosses that boundary -
    // matches ADI's own AppCHRONOAMPCalcVoltage() convention (ADCcode-32768).
    float code = (float)((int32_t)rawCode - 32768);

    // I = V_adc / (PGA * Rtia) where V_adc = code * Vref / 32768
    return (code * Vref_mV / 1000.0f) / (PGA_G * Rtia_Ohm * 32768.0f);
}

/**
 * @brief Executes the full Chronoamperometry (CA) measurement sequence and streams data to Serial.
 */
void C_CA::Run()
{
    Utils_SetStatusLed(CYAN); // CYAN indicates Chronoamperometry sweep active
    Serial.println("CA_START");
    
    // Calculate total steps required
    m_pData->CA_NumSamples = (uint32_t)(m_pData->CA_Duration_s * m_pData->CA_SampleRate_Hz);
    if (m_pData->CA_NumSamples < 1) m_pData->CA_NumSamples = 1;

    // Load DC voltage level
    ConfigDCMeasurement(m_pData->CA_Voltage_mV);
    
    // Execute sampling loops
    for (uint32_t i = 0; i < m_pData->CA_NumSamples; ++i)
    {
        uint32_t raw = MeasureCurrentRaw();
        float current_A = RawToCurrent(raw);
        float time_s = (float)i / m_pData->CA_SampleRate_Hz;

        // Format: CA,<time_seconds>,<current_Amps>,<raw_adc_code_debug>
        char buf[100];
        snprintf(buf, sizeof(buf), "CA,%.4f,%.4e,0x%08lX", time_s, current_A, (unsigned long)raw);
        Serial.println(buf);
        
        // Block until next sampling interval
        delay((int)(1000.0f / m_pData->CA_SampleRate_Hz));
    }

    // Power down converters
    AD5940_AFECtrlS(AFECTRL_ADCPWR | AFECTRL_ADCCNV | AFECTRL_WG | AFECTRL_DACREFPWR, bFALSE);
    
    Utils_SetStatusLed(GREEN); // GREEN indicates finished
    Serial.println("CA_END");
}
