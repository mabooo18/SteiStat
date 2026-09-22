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
extern uint32_t SetDACLevel(float mV);

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
 * @param voltage_mV The target potential to apply across WE and RE.
 */
void C_CA::ConfigDCMeasurement(float voltage_mV)
{
    // Turn off active wave generators to isolate the DC baseline
    AD5940_AFECtrlS(AFECTRL_WG, bFALSE);
    
    // Configure DC output values
    WGCfg_Type wgInit;
    wgInit.WgType = WGTYPE_MMR;
    wgInit.GainCalEn = bTRUE;
    wgInit.OffsetCalEn = bFALSE;
    wgInit.WgCode = SetDACLevel(voltage_mV); // Set DC voltage
    AD5940_WGCfgS(&wgInit);

    // Switch Matrix mappings: route CE0 to Counter, RE0 to Reference, SE0 to Working
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

    // Power up essential analog block amplifiers
    AD5940_AFECtrlS(AFECTRL_DACREFPWR | AFECTRL_EXTBUFPWR | AFECTRL_INAMPPWR |
                    AFECTRL_HSTIAPWR | AFECTRL_HSDACPWR | AFECTRL_DCBUFPWR, bTRUE);
                    
    // Configure ADC Multiplexer to measure across the HS TIA inputs
    AD5940_ADCMuxCfgS(ADCMUXP_HSTIA_P, ADCMUXN_HSTIA_N);

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

    // Route the SINC2-ready source into INTC1 - without this,
    // AD5940_TakeMeasurement()'s ready-flag poll in MeasureCurrentRaw() can
    // never see a true flag and always times out returning a stale 0.
    AD5940_INTCCfg(AFEINTC_1, AFEINTSRC_ALLINT, bTRUE);
    AD5940_INTCClrFlag(AFEINTSRC_ALLINT);
}

/**
 * @brief Triggers a single ADC conversion cycle to measure current.
 * @return The raw digital output code from the Sinc2 filter.
 */
uint32_t C_CA::MeasureCurrentRaw()
{
    // Start conversion
    AD5940_AFECtrlS(AFECTRL_ADCPWR | AFECTRL_ADCCNV, bTRUE);
    delayMicroseconds(500); // Wait briefly for stabilization
    
    int32_t time_out = 1000;
    uint32_t result = AD5940_TakeMeasurement(AFECTRL_ADCPWR | AFECTRL_ADCCNV,
                                             AFEINTSRC_SINC2RDY,
                                             AFERESULT_SINC2,
                                             &time_out);
                                             
    // Stop conversion
    AD5940_AFECtrlS(AFECTRL_ADCCNV, bFALSE);
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
    float PGA_G = 1.0f;                     // PGA gain is 1x
    float Rf_Ohm = 200.0f;                  // Default resistor fallback
    
    // Look up resistor value based on selection index
    uint32_t rf_values[] = {200, 1000, 5000, 10000, 20000, 40000, 80000, 160000};
    if (tia_rf < 8) Rf_Ohm = rf_values[tia_rf];
    
    // Interpret the 16-bit code as a signed two's complement integer
    float code = (int16_t)(rawCode & 0xFFFF);
    
    // I = V_adc / (PGA * Rf) where V_adc = code * Vref / 32768
    return (code * Vref_mV / 1000.0f) / (PGA_G * Rf_Ohm * 32768.0f);
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
        
        // Format: CA,<time_seconds>,<current_Amps>
        char buf[100];
        snprintf(buf, sizeof(buf), "CA,%.4f,%.4e", time_s, current_A);
        Serial.println(buf);
        
        // Block until next sampling interval
        delay((int)(1000.0f / m_pData->CA_SampleRate_Hz));
    }

    // Power down converters
    AD5940_AFECtrlS(AFECTRL_ADCPWR | AFECTRL_ADCCNV | AFECTRL_WG | AFECTRL_DACREFPWR, bFALSE);
    
    Utils_SetStatusLed(GREEN); // GREEN indicates finished
    Serial.println("CA_END");
}
