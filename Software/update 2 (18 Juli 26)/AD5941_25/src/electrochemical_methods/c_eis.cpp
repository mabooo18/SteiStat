// Electrochemical Impedance Spectroscopy (EIS) implementation module.
// Sweeps AC frequency logarithmically and computes impedance vectors via DFT.

#include "c_eis.h"
#include "../ad5940/ad5940.h"
#include "../ad5940/debug.h"
#include "../setup/ad5941_setup.h"
#include "../hardware/wave_gen.h"
#include "../hardware/adc_control.h"
#include "../../utilities.h"
#include "../data_storage/measurement_buffer.h"
#include "../utils/status_utils.h"

// Access global variables from the orchestrator
extern ADCFilterCfg_Type adc_filter;
extern ADCBaseCfg_Type adc_base;
extern HSLoopCfg_Type HpLoopCfg;
extern CLKCfg_Type clk_cfg;
extern uint8_t pga_gain;
extern uint8_t tia_rf;
extern uint16_t amplitude;
extern uint16_t vbias;
extern uint16_t offset;
extern uint16_t settling_delay_ms;
extern uint16_t adc_delay_ms;
extern uint32_t settling_parameter;
extern uint32_t freqlo;
extern uint32_t freqhi;
extern uint16_t nfreqs;
extern uint8_t vzero;
extern float fRcal;
extern float fAmplitude;
extern float fBias;
extern float fOffset;
extern uint8_t EIS_mode;
extern bool SeeedStatMode;
extern uint32_t verbose;
extern uint32_t AD5940_TakeMeasurement(uint32_t afectrl_bits, uint32_t afectrl_readybit, uint32_t afectrl_resultType, int32_t *time_out);

/**
 * @brief Evaluation check function that checks if AFE is busy calculating.
 * @return True if DFT processing is still active.
 */
static bool IsAD5940Calculating()
{
    return !(AD5940_INTCTestFlag(AFEINTC_0, AFEINTSRC_DATAFIFOTHRESH));
}

/**
 * @brief Configures Low-Power Loop parameters (LP DAC, LP TIA routing, bias).
 */
static void Config_LPLOOP()
{
    LPDACCfg_Type lpdac_cfg;
    lpdac_cfg.LpdacSel = LPDAC0;
    lpdac_cfg.LpDacVbiasMux = LPDACVBIAS_12BIT; // Sets Reference Electrode bias
    lpdac_cfg.LpDacVzeroMux = LPDACVZERO_6BIT;  // Sets Working Electrode bias (vzero)
    lpdac_cfg.DacData6Bit = vzero;
    lpdac_cfg.DacData12Bit = vbias;
    lpdac_cfg.DataRst = bFALSE;
    lpdac_cfg.LpDacSW = LPDACSW_VBIAS2LPPA | LPDACSW_VBIAS2PIN | LPDACSW_VZERO2LPTIA | LPDACSW_VZERO2PIN | LPDACSW_VZERO2HSTIA;
    lpdac_cfg.LpDacRef = LPDACREF_2P5;          // Set base 2.5V reference
    lpdac_cfg.LpDacSrc = LPDACSRC_MMR;
    lpdac_cfg.PowerEn = bTRUE;
    AD5940_LPDACCfgS(&lpdac_cfg);
}

/**
 * @brief Runs offset calibrations for the PGA and calibrations for each range of the High-Speed DAC.
 * @param power_mode Mode settings (0 = Low-Power, 1 = High-Power).
 * @param pga_g PGA gain configuration codes.
 */
static void Calibrate_HSDAC(uint8_t power_mode, uint8_t pga_g)
{
    HSDACCal_Type hsdac_cal;
    ADCPGACal_Type adcpga_cal;
    CLKCfg_Type local_clk_cfg;

    AD5940_AFEPwrBW(power_mode, AFEBW_250KHZ);

    // Set clock settings to ensure calibration happens at stable reference speeds
    local_clk_cfg.ADCClkDiv = ADCCLKDIV_1;
    local_clk_cfg.ADCCLkSrc = ADCCLKSRC_HFOSC;
    local_clk_cfg.SysClkDiv = SYSCLKDIV_1;
    local_clk_cfg.SysClkSrc = SYSCLKSRC_HFOSC;
    local_clk_cfg.HfOSC32MHzMode = bTRUE;
    local_clk_cfg.HFOSCEn = bTRUE;
    local_clk_cfg.HFXTALEn = bFALSE;
    local_clk_cfg.LFOSCEn = bTRUE;
    AD5940_CLKCfg(&local_clk_cfg);

    // ADC PGA Offset Calibration
    adcpga_cal.AdcClkFreq = 16000000;
    adcpga_cal.ADCPga = pga_g;
    adcpga_cal.ADCSinc2Osr = ADCSINC2OSR_1333;
    adcpga_cal.ADCSinc3Osr = ADCSINC3OSR_4;
    adcpga_cal.PGACalType = PGACALTYPE_OFFSET;
    adcpga_cal.TimeOut10us = 1000;
    adcpga_cal.VRef1p11 = 1.11;
    adcpga_cal.VRef1p82 = 1.82;
    AD5940_ADCPGACal(&adcpga_cal);

    // Calibrate 607mV DAC range
    hsdac_cal.ExcitBufGain = EXCITBUFGAIN_2;
    hsdac_cal.HsDacGain = HSDACGAIN_1;
    hsdac_cal.AfePwrMode = power_mode;
    hsdac_cal.ADCSinc2Osr = ADCSINC2OSR_1333;
    hsdac_cal.ADCSinc3Osr = ADCSINC3OSR_4;
    AD5940_HSDACCal(&hsdac_cal);

    // Recalibrate PGA offsets
    adcpga_cal.ADCPga = pga_g;
    AD5940_ADCPGACal(&adcpga_cal);

    // Calibrate 125mV range
    hsdac_cal.ExcitBufGain = EXCITBUFGAIN_2;
    hsdac_cal.HsDacGain = HSDACGAIN_0P2;
    AD5940_HSDACCal(&hsdac_cal);

    // Calibrate 75mV range
    hsdac_cal.ExcitBufGain = EXCITBUFGAIN_0P25;
    hsdac_cal.HsDacGain = HSDACGAIN_1;
    AD5940_HSDACCal(&hsdac_cal);

    // Calibrate 15mV range
    hsdac_cal.ExcitBufGain = EXCITBUFGAIN_0P25;
    hsdac_cal.HsDacGain = HSDACGAIN_0P2;
    AD5940_HSDACCal(&hsdac_cal);
}

/**
 * @brief Binds the class instance to the global parameters state storage.
 * @param pData Reference to the global C_DataStorage instance.
 */
void C_EIS::Begin(C_DataStorage* pData)
{
    m_pData = pData;
}

/**
 * @brief Executes an EIS sweep using the current frequency limits and parameters.
 */
void C_EIS::Run()
{
    Utils_SetStatusLed(WHITE); // Display white during calibration
    Calibrate_HSDAC(0, pga_gain);

    if (m_pData->EIS_Mode == 0) Utils_SetStatusLed(BLUE);   // Blue signals working cell impedance sweeps
    else Utils_SetStatusLed(RED);                           // Red signals internal resistor calibrations

    // Calculate logarithmic frequency steps
    float flo = (float)m_pData->FreqLo / 1000.0f;
    float fhi = (float)m_pData->FreqHi / 1000.0f;
    float log_start = log10(flo);
    float log_end = log10(fhi);
    float log_step = (log_end - log_start) / (m_pData->NFreqs - 1);

    for (uint16_t i = 0; i < m_pData->NFreqs; ++i)
    {
        // Re-initialize drivers at start of step
        AD5941_InitAll();
        Config_LPLOOP(); // Apply loop bias settings

        float frequency = pow(10.0f, log_start + i * log_step);
        Do_WaveGen(m_pData->EIS_Mode, frequency, amplitude, tia_rf);

        // Adjust clock rates and enable High-Power Mode if frequency > 80 kHz
        if (frequency > 80000.0f)
        {
            HpLoopCfg.HsDacCfg.ExcitBufGain = EXCITBUFGAIN_2;
            HpLoopCfg.HsDacCfg.HsDacGain = HSDACGAIN_1;
            HpLoopCfg.HsDacCfg.HsDacUpdateRate = 0x07;
            AD5940_HSLoopCfgS(&HpLoopCfg);
            
            clk_cfg.HfOSC32MHzMode = bTRUE; // Switch clock to 32MHz mode
            AD5940_CLKCfg(&clk_cfg);
            AD5940_HPModeEn(bTRUE);         // Activate High-Power mode
        }
        else
        {
            HpLoopCfg.HsDacCfg.ExcitBufGain = EXCITBUFGAIN_2;
            HpLoopCfg.HsDacCfg.HsDacGain = HSDACGAIN_1;
            HpLoopCfg.HsDacCfg.HsDacUpdateRate = 0x1B;
            AD5940_HSLoopCfgS(&HpLoopCfg);
            
            clk_cfg.HfOSC32MHzMode = bFALSE;// Clock remains in 16MHz mode
            AD5940_CLKCfg(&clk_cfg);
            AD5940_HPModeEn(bFALSE);        // Deactivate High-Power mode
        }

        init_AD5940_ADC(frequency); // Configure ADC filter parameters for this step frequency

        if (m_pData->SeeedStatMode)
        {
            digitalWrite(D4, HIGH); // Pulse D4 high to show start of delay
        }

        // Apply selected settling delay to wait for signal stabilization
        if (settling_parameter > 1) Delay(settling_parameter, 16, 0, 16);
        else if (settling_parameter == 1)
        {
            if (frequency < 100.0f) settling_delay_ms = (uint16_t)(1000.0f / frequency);
            else settling_delay_ms = 10;
            Delay(settling_delay_ms, 16, 0, 16);
        }

        if (m_pData->SeeedStatMode) digitalWrite(D4, LOW);

        // Power up ADC converter
        AD5940_AFECtrlS(AFECTRL_ADCCNV | AFECTRL_DFT, bFALSE);
        AD5940_AFECtrlS(AFECTRL_ADCPWR, bTRUE);
        if (adc_delay_ms) Delay(adc_delay_ms, -16, 0, -16);

        AD5940_INTCCfg(AFEINTC_0, AFEINTSRC_ALLINT, bTRUE);
        AD5940_INTCClrFlag(AFEINTSRC_ALLINT);
        
        digitalWrite(D4, HIGH);
        AD5940_AFECtrlS(AFECTRL_ADCCNV | AFECTRL_DFT, bTRUE);  // Begin conversions and DFT engine
        Delay(&IsAD5940Calculating, 16, 0, 16);                 // Wait until DFT processing finishes
        AD5940_AFECtrlS(AFECTRL_ADCCNV | AFECTRL_DFT, bFALSE); // End conversions

        // Read real and imaginary components from FIFO registers
        uint32_t dftReal = AD5940_ReadReg(REG_AFE_DATAFIFORD);
        uint32_t dftImag = AD5940_ReadReg(REG_AFE_DATAFIFORD);
        digitalWrite(D4, LOW);

        AddMeasurementToHistory(dftReal, dftImag);
        if (AD5940_INTCTestFlag(AFEINTC_0, AFEINTSRC_ADCMINERR | AFEINTSRC_ADCMAXERR)) OutputPulse(D5, 10);
        AD5940_INTCClrFlag(AFEINTSRC_ALLINT);

        if (m_pData->SeeedStatMode)
        {
            // Stream in ASCII format
            float real = ToFloat(dftReal);
            float imag = ToFloat(dftImag);
            if (verbose & 1)
            {
                Serial.print(i + 1);
                Serial.print("=");
                Serial.print(real);
                Serial.print(",");
                Serial.print(imag);
                Serial.println(",");
            }
            AppendMeasurement(real, imag);
        }
        else
        {
            // Stream in raw binary format (4 bytes per component)
            Serial.write((uint8_t*)&dftReal, 4);
            Serial.write((uint8_t*)&dftImag, 4);
        }
    }

    Utils_SetStatusLed(GREEN); // GREEN indicates finished
    Utils_SetStatusPixels(0, 255, 0);
}

/**
 * @brief Coordinates the dual-sweep SeeedStat compatibility routine.
 */
void C_EIS::RunSeeedStat()
{
    AD5940_PGA_Calibration();
    
    ResetMeasurementBuffer();
    m_pData->EIS_Mode = 0; // Sweep Rz
    Run();
    
    ResetMeasurementBuffer();
    m_pData->EIS_Mode = 1; // Sweep Rcal
    Run();
    
    CalculateNyquistCurve(); // Map result pairs into Nyquist plane coordinates
}

/**
 * @brief Computes impedance vectors and projects polar coordinates to Cartesian coordinates.
 */
void C_EIS::CalculateNyquistCurve()
{
    float* pRZ = m_pData->Measurements;
    float* pRCAL = &m_pData->Measurements[2 * m_pData->NumberOfMeasurements];
    
    for (int i = 0; i < m_pData->NumberOfMeasurements; ++i)
    {
        float RZmag, RZphase;
        float RCALmag, RCALphase;
        
        // Read cell components
        float real = *pRZ++;
        float imag = *pRZ++;
        RZmag = sqrt(real * real + imag * imag);
        RZphase = atan2(-imag, real);
        
        // Read reference components
        real = *pRCAL++;
        imag = *pRCAL++;
        RCALmag = sqrt(real * real + imag * imag);
        RCALphase = atan2(-imag, real);
        
        // Scale magnitudes relative to fRcal
        float ZUnknownMag = abs(RCALmag / RZmag) * fRcal;
        float ZUnknownPhase = RZphase - RCALphase;
        
        // Convert to Cartesian plane coordinates
        float nyquistPointX = ZUnknownMag * cos(ZUnknownPhase);
        float nyquistPointY = ZUnknownMag * sin(ZUnknownPhase);
        
        // Format and write output
        char buf[100];
        sprintf(buf, "%.3f,%.3f,", nyquistPointX, nyquistPointY);
        Serial.println(buf);
        Serial.flush();
        delay(1);
    }
}
