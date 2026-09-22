// Open Circuit Potential (OCP) implementation module.
// Configures reference electrode to float and evaluates the potential between RE and WE.

#include "c_ocp.h"
#include "../ad5940/ad5940.h"
#include "../ad5940/debug.h"
#include "../setup/ad5941_setup.h"
#include "../../utilities.h"
#include "../../src/interface/led_interface.h"
#include "../../src/utils/status_utils.h"

// Access functions and variables from the main orchestrator
extern uint32_t AD5940_TakeMeasurement(uint32_t afectrl_bits, uint32_t afectrl_readybit, uint32_t afectrl_resultType, int32_t *time_out);
extern uint32_t SetDACLevel(float mV);
extern ADCFilterCfg_Type adc_filter;
extern ADCBaseCfg_Type adc_base;
extern HSLoopCfg_Type HpLoopCfg;
extern uint32_t ADCCON;
extern uint32_t HSDACDAT;
extern uint32_t OCP_sum;
extern bool SeeedStatMode;
extern float WEmV;
extern float WEfrom;
extern float WEto;
extern float WEstep;
extern BoolFlag ocpCalibration;
extern BoolFlag ocpCalibrationCycling;
extern float constA;
extern float constB;
extern bool useConstAB;
extern uint16_t OCP_npts;
extern uint8_t vzero;
extern uint16_t vbias;
extern uint16_t amplitude;
extern uint16_t offset;
extern uint16_t adc_delay_ms;
extern uint16_t settling_delay_ms;
extern uint32_t settling_parameter;
extern float fRcal;
extern float fAmplitude;
extern float fBias;
extern float fOffset;
extern uint8_t pga_gain;
extern uint8_t tia_rf;
extern uint8_t EIS_mode;
extern uint16_t nfreqs;
extern uint32_t freqlo;
extern uint32_t freqhi;
extern uint32_t _CGmax;
extern uint32_t _CGmin;

/**
 * @brief Configures High-Speed loop excitation paths during OCP calibration.
 * @param hsDacDat DAC configuration word.
 */
static void ConfigureHSLoopCfg(uint32_t hsDacDat)
{
    HSLoopCfg_Type loopCfg;
    AD5940_StructInit(&loopCfg, sizeof(HSLoopCfg_Type));
    
    // Set DAC buffer and gain scales
    loopCfg.HsDacCfg.ExcitBufGain = EXCITBUFGAIN_2;
    loopCfg.HsDacCfg.HsDacGain = HSDACGAIN_1;
    loopCfg.HsDacCfg.HsDacUpdateRate = 7;
    
    // HS TIA configuration - connect feedback resistor paths
    loopCfg.HsTiaCfg.DiodeClose = bFALSE;
    loopCfg.HsTiaCfg.HstiaBias = HSTIABIAS_1P1;
    loopCfg.HsTiaCfg.HstiaCtia = 16;
    loopCfg.HsTiaCfg.HstiaDeRload = HSTIADERLOAD_OPEN;
    loopCfg.HsTiaCfg.HstiaDeRtia = HSTIADERTIA_TODE;
    loopCfg.HsTiaCfg.HstiaRtiaSel = HSTIARTIA_200;
    
    // Mappings: Route paths for CE0 and SE0LOAD
    loopCfg.SWMatCfg.Dswitch = SWD_CE0;
    loopCfg.SWMatCfg.Pswitch = SWP_CE0;
    loopCfg.SWMatCfg.Nswitch = SWN_SE0LOAD;
    loopCfg.SWMatCfg.Tswitch = SWT_TRTIA | SWT_SE0LOAD;
    
    // MMR configuration
    loopCfg.WgCfg.WgType = WGTYPE_SIN;
    loopCfg.WgCfg.GainCalEn = bFALSE;
    loopCfg.WgCfg.OffsetCalEn = bFALSE;
    loopCfg.WgCfg.WgCode = hsDacDat;
    AD5940_HSLoopCfgS(&loopCfg);
}

/**
 * @brief Binds the class instance to the global parameters state storage.
 * @param pData Reference to the global C_DataStorage instance.
 */
void C_OCP::Begin(C_DataStorage* pData)
{
    m_pData = pData;
}

/**
 * @brief Configures AFE registers specifically for OCP tracking.
 */
void C_OCP::Configure()
{
    // Perform gain calibration and set low-power amplifier bandwidth settings
    AD5940_PGA_Calibration();
    AD5940_AFEPwrBW(AFEPWR_LP, AFEBW_250KHZ);

    // Set ADC PGA gain to 1.5x
    adc_base.ADCPga = ADCPGA_1P5;
    AD5940_ADCBaseCfgS(&adc_base);

    // Set ADC filters: Sinc3 OSR = 4, Sinc2 OSR = 1333, Notch filter active
    adc_filter.ADCSinc3Osr = ADCSINC3OSR_4;
    adc_filter.ADCSinc2Osr = ADCSINC2OSR_1333;
    adc_filter.ADCAvgNum = ADCAVGNUM_2;
    adc_filter.ADCRate = ADCRATE_800KHZ;
    adc_filter.BpNotch = bTRUE;
    adc_filter.BpSinc3 = bFALSE;
    adc_filter.Sinc2NotchEnable = bTRUE;
    AD5940_ADCFilterCfgS(&adc_filter);

    // Route Mux: Positive to SE0 (Working), Negative to AIN3 (Reference)
    AD5940_ADCMuxCfgS(ADCMUXP_VSE0, ADCMUXN_AIN3);

    // Set interrupts flags
    AD5940_INTCCfg(AFEINTC_1, AFEINTSRC_ALLINT, bTRUE);
    AD5940_INTCClrFlag(AFEINTSRC_ALLINT);

    // Output diagnostics pulse pin D4
    OutputPulse(D4, 300);
    if (m_pData->OCP_Calibration == bTRUE)
    {
        digitalWrite(D4, HIGH);
        uint32_t hsDacDat = SetDACLevel(m_pData->WE_mV);
        digitalWrite(D4, LOW);
        ConfigureHSLoopCfg(hsDacDat);
    }

    // Toggle power switches to low-power buffers depending on calibration state
    BoolFlag enable = m_pData->OCP_Calibration ? bTRUE : bFALSE;
    AD5940_AFECtrlS(AFECTRL_DACREFPWR, enable);
    AD5940_AFECtrlS(AFECTRL_EXTBUFPWR | AFECTRL_INAMPPWR | AFECTRL_HSTIAPWR | AFECTRL_HSDACPWR, enable);
    AD5940_AFECtrlS(AFECTRL_WG, enable);
    AD5940_AFECtrlS(AFECTRL_DCBUFPWR, enable);
}

/**
 * @brief Performs a single ADC conversion cycle.
 * @return Raw output code from the Sinc2 filter.
 */
uint32_t C_OCP::Do1Measurement()
{
    digitalWrite(D5, HIGH); // Assert GPIO D5 high to show active conversion phase
    int32_t time_out = 1000;
    uint32_t result = AD5940_TakeMeasurement(AFECTRL_ADCPWR | AFECTRL_ADCCNV,
                                             AFEINTSRC_SINC2RDY,
                                             AFERESULT_SINC2,
                                             &time_out);
    digitalWrite(D5, LOW);
    return result;
}

/**
 * @brief Coordinates OCP measurement cycles.
 */
void C_OCP::Measure()
{
    Utils_SetStatusLed(MAGENTA); // MAGENTA signals OCP active

    // Start ADC conversions
    AD5940_AFECtrlS(AFECTRL_ADCPWR | AFECTRL_ADCCNV, bTRUE);
    delay(5);

    digitalWrite(D4, HIGH);
    if (m_pData->OCP_CalibrationCycling == bTRUE)
    {
        // Sweep target voltages sequentially to calibrate equations
        float step = m_pData->WEFrom_mV < m_pData->WETo_mV ? abs(m_pData->WEStep_mV) : -abs(m_pData->WEStep_mV);
        for (float we = m_pData->WEFrom_mV; m_pData->WEFrom_mV < m_pData->WETo_mV ? we <= m_pData->WETo_mV : we >= m_pData->WETo_mV; we += step)
        {
            delayMicroseconds(500);
            digitalWrite(D4, HIGH);
            SetDACLevel(we); // Apply potential step

            uint32_t ocp_sum = 0;
            for (uint16_t i = 0; i < m_pData->OCP_Npts; ++i)
            {
                ocp_sum += Do1Measurement();
            }

            m_pData->OCP_Sum = ocp_sum;
            digitalWrite(D4, LOW);
        }
    }
    else
    {
        // Perform simple static measurement session
        m_pData->OCP_Sum = 0;
        for (uint16_t i = 0; i < m_pData->OCP_Npts; ++i)
        {
            delayMicroseconds(500);
            m_pData->OCP_Sum += Do1Measurement();
        }
    }

    // Stop conversions
    AD5940_AFECtrlS(AFECTRL_ADCPWR | AFECTRL_ADCCNV, bFALSE);
    digitalWrite(D4, LOW);

    // Read and store ADC control configuration register
    m_pData->ADCCON = AD5940_ReadReg(REG_AFE_ADCCON);
    if (m_pData->OCP_CalibrationCycling != bTRUE)
    {
        Serial.print('Z'); // Transmit completion token
    }

    m_pData->OCP_CalibrationCycling = false;
    m_pData->SeeedStatMode = false;
    Utils_SetStatusLed(GREEN);
}

/**
 * @brief Translates raw OCP counts into cell potential in millivolts.
 * @return Decoded cell potential in mV.
 */
float C_OCP::Calculate()
{
    float ocp_mV;
    float ocp_1 = static_cast<float>(m_pData->OCP_Sum) / m_pData->OCP_Npts;
    if (m_pData->SeeedStatMode)
    {
        ocp_1 *= -1.0f; // Invert to represent Vre - Vwe instead of Vwe - Vre
    }

    if (m_pData->UseConstAB)
    {
        // Decode potential using calibrated slope and offset equations
        ocp_mV = (ocp_1 - m_pData->ConstA) / m_pData->ConstB;
    }
    else
    {
        // Decode potential using direct physical registers math
        uint8_t GNPGA = (m_pData->ADCCON & BITM_AFE_ADCCON_GNPGA) >> BITP_AFE_ADCCON_GNPGA;
        float Vref_mV = (GNPGA == 1) ? 1835.0f : 1820.0f;       // Select Vref voltage based on PGA gain level
        float PGA_G_values[] = { 1.0f, 1.5f, 2.0f, 4.0f, 9.0f, 9.0f };
        float PGA_G = PGA_G_values[GNPGA];                      // Resolve PGA stage scale
        
        uint8_t MUXSELN = (m_pData->ADCCON & BITM_AFE_ADCCON_MUXSELN) >> BITP_AFE_ADCCON_MUXSELN;
        const uint8_t MUXSELN_VBIAS_CAP = 8;
        float VBIAS_CAP_mV = (MUXSELN == MUXSELN_VBIAS_CAP) ? 1110.0f : 0.0f;
        const float ADC_MIDDLE_AND_ACTIVE_RANGE = 1 << 15;      // Subtract 15-bit offset
        
        // Final standard physical equation conversion
        ocp_mV = (ocp_1 - ADC_MIDDLE_AND_ACTIVE_RANGE) * (Vref_mV / PGA_G) / ADC_MIDDLE_AND_ACTIVE_RANGE + VBIAS_CAP_mV;
    }

    return ocp_mV;
}
