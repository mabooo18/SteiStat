/*
    AUTHOR: Kent

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to deal
    in the Software without restriction, including without limitation the rights to
    use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
    of the Software, and to permit persons to whom the Software is furnished to do
    so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in all
    copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
    SOFTWARE.
*/

// Versioning and pin output feature toggles
#define USE_250202_Version  1
#define SET_VOLTAGE_ON_RP2040_D6  0

// AD5940/AD5941 Driver Library Headers
#include "ad5940.h"
#include "AD5940.h"
#include <stdio.h>
#include "string.h"
#include <Adafruit_NeoPixel.h>
#include "utilities.h"
#include "hunstat_status_utils.h"
#include "src/ad5940/debug.h"

// Project Modular Component Headers
#include "hunstat_data_storage.h"
#include "src/data_storage/measurement_buffer.h"
#include "src/electrochemical_methods/electrochemical_methods.h"
#include "src/command_processing/command_processing.h"
#include "src/setup/ad5941_setup.h"
#include "src/communication/communication.h"
#include "src/hardware/adc_control.h"
#include "src/hardware/wave_gen.h"
#include "src/hardware/gain_control.h"
#include "src/interface/led_interface.h"

// Take a single measurement with the specified AFE control configurations
extern uint32_t AD5940_TakeMeasurement(uint32_t afectrl_bits, uint32_t afectrl_readybit, uint32_t afectrl_resultType, int32_t *time_out);

// Global CV scan variables exported from/to cv.cpp
extern float V_start;
extern float V_stop;
extern float Estep;             /**< Potential difference between steps in mV. Ideally a multiple of DAC LSB */
extern float ScanRate;          /**< Ramp slope in mV/sec */
extern uint16_t CycleNumber;    /**< Number of CV cycles to perform */
extern void  cvSetup (float start, float stop);

// NeoPixel LED Configurations for Status Display
int Power = 11;
int PIN  = 12;
#define NUMPIXELS 1
Adafruit_NeoPixel pixels(NUMPIXELS, PIN, NEO_GRB + NEO_KHZ800);

// Global Modular Class Instances
C_DataStorage g_Data;     // System parameters and calibration state manager
C_Communication g_Comm;   // Command interpreter and Serial interface processor
C_AD5941_Setup g_Setup;   // Core hardware startup and initialization orchestrator

// RP2040 Microcontroller Native ADC Pin Declarations (used for independent OCP verification)
#define WEpin A0            // Pin A0: Measures Working Electrode potential
#define REpin A3            // Pin A3: Measures Reference Electrode potential

// ADC constants for RP2040 (3.3V reference, 12-bit resolution)
const float ADC_AVDD = 3300.0;              
const int ADCresolution = 12;               
const int ADCFullValue = (1 << ADCresolution) - 1;

// OCP (Open Circuit Potential) Sweep and Calibration parameters
float WEmV = 0.0;                           // Expected WE voltage target set during OCP calibration
float WEfrom = 0.0;                         // Calibration scan range start voltage
float WEto = 0.0;                           // Calibration scan range end voltage
float WEstep = 0.0;                         // Calibration scan step voltage size
uint32_t HSDACDAT;                          // Buffer holding calculated High-Speed DAC registers
BoolFlag ocpCalibration = bFALSE;           // True if OCP calibration is active
BoolFlag ocpCalibrationCycling = bFALSE;    // True if performing sequential multi-point OCP calibrations
float constA = 32772.0;                     // Linear equation offset factor A (from OCP calibration fit)
float constB = -26.719;                     // Linear equation slope factor B (from OCP calibration fit)
bool useConstAB = false;                    // Uses constA & constB for OCP if true, registers math otherwise

#define ADCPGA_GAIN_SEL ADCPGA_1P5          // Default ADC PGA gain factor select (1.5x gain)

// ADC and Waveform settling delays (in milliseconds) used during EIS cycles
uint32_t settling_parameter = 2;            // Delay before ADC sampling to let waveforms settle
uint16_t adc_delay_ms = 1;                  // Delay to allow ADC power circuits to stabilize
uint32_t SYS_CLOCK_HZ = 16000000UL;         // Base system clock (16 MHz oscillator)

// Global SPI control pins (defined in XIAOPort.cpp)
extern int csPin;
extern int resetPin;
extern int intPin;

// Serial parsing flags
int receivedNumber = 0;
char commandType = 0;
bool hex = false;

// Frequency sweep structure definition
FreqParams_Type FP;

// Gain, TIA, and general sweep configurations
uint8_t pga_gain = 1, tia_rf = 3, EIS_mode, vzero = 26, use_variable_gain = 0;   // EIS_mode: 0 = Rz, 1 = Rcal
uint16_t nfreqs = 50, vbias = 1664, amplitude = 126, offset = 4040, OCP_npts;
uint16_t settling_delay_ms;                                                     // Loop delay variable
uint32_t freqlo, freqhi, _CGmax = 30000, _CGmin = 7500, OCP_sum, ADCCON;

uint32_t ADCFILTERCON, DFTCON, AFECON;

AFERefCfg_Type aferef_cfg;

// Calibration Resistor (RCAL) value mounted on board (in Ohms)
float fRcal = 10000.0;
float fAmplitude = 50.0,
      fBias = 0.0,
      fOffset = 0.0;

// High-speed excitation and TIA/switch structures
HSLoopCfg_Type HpLoopCfg;
ADCFilterCfg_Type adc_filter;
ADCBaseCfg_Type adc_base;
DFTCfg_Type DftCfg;
FIFOCfg_Type fifo_cfg;
CLKCfg_Type clk_cfg;
ClksCalInfo_Type clks_cal;

extern uint32_t verbose;  // Logging detail level flags

// SeedStat compatibility flag (ASCII vs. Raw binary serial stream)
bool SeeedStatMode = false;

extern char history[];
extern char* pHistory;

#if SET_VOLTAGE_ON_RP2040_D6
/**
 * @brief Sets a target analog voltage on an RP2040 pin via high-resolution PWM.
 * @param pin The target GPIO pin number.
 * @param mV The target voltage in millivolts.
 * @param withDelay If true, delays execution by 3 seconds for stabilization.
 */
void RP2040_SetVoltageOnPin(int pin, float mV, bool withDelay)
{
    if (mV <= 1.0)
    {
        pinMode(pin, INPUT); // Disable pin output if target voltage is negligible
    }
    else
    {
        pinMode(pin, OUTPUT);
        const int resolution = 16;                          // 16-bit PWM DAC resolution
        analogWriteResolution(resolution);
        int value = ((1 << resolution) - 1) * mV / 3300.0;  // Calculate duty cycle relative to 3.3V VDD
        analogWrite(pin, value);                            
    }
    
    if (withDelay)
    {
        delay(3000);
    }
}
#endif

/**
 * @brief Calculates the High-Speed DAC binary code from a millivolt value and writes it.
 * @param mV The desired DC target potential in mV.
 * @return The 12-bit code loaded into the HSDAC data register.
 */
uint32_t SetDACLevel(float mV)
{
    // Read the current configuration to evaluate attenuation and gain scales
    uint32_t HSDACCON = AD5940_ReadReg(REG_AFE_HSDACCON);
    float INAMPGNMDE = (HSDACCON & BITM_AFE_HSDACCON_INAMPGNMDE) == BITM_AFE_HSDACCON_INAMPGNMDE ? 0.25 : 2.0;
    float ATTENEN = (HSDACCON & BITM_AFE_HSDACCON_ATTENEN) == BITM_AFE_HSDACCON_ATTENEN ? 0.2 : 1.0;
    
    // Scale input voltage into binary DAC code for MMR (Memory Mapped Register) write mode
    HSDACDAT = mV * 2047.0 / (808.0 * INAMPGNMDE * ATTENEN);     

    // Load code into the Waveform Generator initialization structure
    WGCfg_Type wgInit;
    wgInit.WgType = WGTYPE_MMR;         // Write directly to the MMR register instead of generator
    wgInit.GainCalEn = bTRUE;           // Apply internal gain calibration coefficients
    wgInit.OffsetCalEn = bFALSE;        
    wgInit.WgCode = HSDACDAT;
    AD5940_WGCfgS(&wgInit);

    // Logging details if verbose mode allows
    Log(0x80, __LINE__, "mV=%.2f HSDACCON=0x%lX INAMPGNMDE=%.2f ATTENEN=%.1f HSDACDAT=%ld(0x%lX) ",
                         mV,     HSDACCON,      INAMPGNMDE,     ATTENEN,     HSDACDAT, constrain(HSDACDAT, 0x200, 0xe00));
                         
#if SET_VOLTAGE_ON_RP2040_D6
    RP2040_SetVoltageOnPin(D6, mV, false);
#endif

    return HSDACDAT;
}

/**
 * @brief Scans for the optimum TIA feedback resistor and PGA gain combinations.
 * @param CGmax Maximum combined gain limit.
 * @param CGmin Minimum combined gain limit.
 * @param freq Operating measurement frequency.
 * @param TIA_Rf [out] Pointer to store the selected TIA RF code.
 * @param PGA_gain [out] Pointer to store the selected PGA gain code.
 * @param closest_CG [out] Pointer to store the closest combined gain found.
 */
void FindOptimum_Rf_PGA(uint32_t CGmax, uint32_t CGmin, float freq, uint8_t* TIA_Rf, uint8_t* PGA_gain, float* closest_CG)
{
    Hardware_FindOptimum_Rf_PGA(CGmax, CGmin, freq, TIA_Rf, PGA_gain, closest_CG);
}

/**
 * @brief Configures the High-Speed loop excitation buffers and switch matrix settings.
 * @param hsDacDat The target high speed DAC level value.
 */
void ConfigureHSLoopCfg(uint32_t hsDacDat)
{
    HSLoopCfg_Type HpLoopCfg;
    AD5940_StructInit(&HpLoopCfg, sizeof(HSLoopCfg_Type));
    
    // Set DAC buffer and gain scales
    HpLoopCfg.HsDacCfg.ExcitBufGain = EXCITBUFGAIN_2;
    HpLoopCfg.HsDacCfg.HsDacGain = HSDACGAIN_1;
    HpLoopCfg.HsDacCfg.HsDacUpdateRate = 7;

    // HS TIA configuration - load settings and connect TIA output to the pin
    HpLoopCfg.HsTiaCfg.DiodeClose = bFALSE;
    HpLoopCfg.HsTiaCfg.HstiaBias = HSTIABIAS_1P1;
    HpLoopCfg.HsTiaCfg.HstiaCtia = 16; /* 16pF capacitor */
    HpLoopCfg.HsTiaCfg.HstiaDeRload = HSTIADERLOAD_OPEN;
    HpLoopCfg.HsTiaCfg.HstiaDeRtia = HSTIADERTIA_TODE;    /* Connect HSTIA output to DE0 pin */
    HpLoopCfg.HsTiaCfg.HstiaRtiaSel = HSTIARTIA_200;

    // Switch Matrix mappings: Route pins for Counter (CE0) and Reference (RE0) paths
    HpLoopCfg.SWMatCfg.Dswitch = SWD_CE0;
    HpLoopCfg.SWMatCfg.Pswitch = SWP_CE0;
    HpLoopCfg.SWMatCfg.Nswitch = SWN_SE0LOAD;
    HpLoopCfg.SWMatCfg.Tswitch = SWT_TRTIA | SWT_SE0LOAD;

    // Generator setup
    HpLoopCfg.WgCfg.WgType = WGTYPE_MMR;    
    HpLoopCfg.WgCfg.GainCalEn = bFALSE;
    HpLoopCfg.WgCfg.OffsetCalEn = bFALSE;
    HpLoopCfg.WgCfg.WgCode = hsDacDat;
    AD5940_HSLoopCfgS(&HpLoopCfg);
}

/**
 * @brief Configures the AD5941 registers specifically for Open Circuit Potential (OCP) tracking.
 * @param wemV The expected Working Electrode voltage used during calibration.
 */
void Config_AD5941_OCP_Measurement(float wemV)
{
    // Calibrate PGA offset and set low-power amplifier bandwidth settings
    AD5940_PGA_Calibration();
    AD5940_AFEPwrBW(AFEPWR_LP, AFEBW_250KHZ);

    // Initialize ADC Base register
    adc_base.ADCPga = ADCPGA_GAIN_SEL;
    AD5940_ADCBaseCfgS(&adc_base);

    // Initialize filter chains: Sinc3 filter followed by Sinc2 + Notch filter
    // Output data rate = 800 kHz / 4 / 1333 = 150.03 Hz
    adc_filter.ADCSinc3Osr = ADCSINC3OSR_4;
    adc_filter.ADCSinc2Osr = ADCSINC2OSR_1333;
    adc_filter.ADCAvgNum = ADCAVGNUM_2;         
    adc_filter.ADCRate = ADCRATE_800KHZ;
    adc_filter.BpNotch = bTRUE;
    adc_filter.BpSinc3 = bFALSE;
    adc_filter.Sinc2NotchEnable = bTRUE;
    AD5940_ADCFilterCfgS(&adc_filter);

    // Route ADC Multiplexer: Positive node to SE0, Negative node to AIN3 (RE pin)
    AD5940_ADCMuxCfgS(ADCMUXP_VSE0, ADCMUXN_AIN3);          
    
    // Configure interrupt routines on INTC1 for data signaling
    AD5940_INTCCfg(AFEINTC_1, AFEINTSRC_ALLINT, bTRUE);
    AD5940_INTCClrFlag(AFEINTSRC_ALLINT);
    
    // Toggle pulse pin D4 to signal start of initialization sequence
    OutputPulse(D4, 300);
    if (ocpCalibration == bTRUE)
    {
        digitalWrite(D4, HIGH);
        uint32_t hsDacDat = SetDACLevel(wemV);
        digitalWrite(D4, LOW);

        ConfigureHSLoopCfg(hsDacDat);
    }

    // Enable/Disable power to reference loops, excitation loops and generators based on calibration state
    AD5940_AFECtrlS(AFECTRL_DACREFPWR, ocpCalibration);
    AD5940_AFECtrlS(AFECTRL_EXTBUFPWR | AFECTRL_INAMPPWR | AFECTRL_HSTIAPWR | AFECTRL_HSTIAPWR | AFECTRL_HSDACPWR, ocpCalibration);
    AD5940_AFECtrlS(AFECTRL_WG, ocpCalibration);
    AD5940_AFECtrlS(AFECTRL_DCBUFPWR, ocpCalibration);

    // Note: Low-Power loop is intentionally not configured here to ensure 
    // the Reference Electrode floats freely relative to the cell during OCP
}

/**
 * @brief Executes a single ADC measurement cycle for OCP evaluation.
 * @return The raw ADC data code from the Sinc2 filter.
 */
uint32_t Do1_AD5941_OCP_Measurement()
{
    uint32_t result = 0;
    digitalWrite(D5, HIGH); // Assert GPIO D5 high to show active conversion phase
#if 0
    // Spinlock polling method
    while(AD5940_INTCTestFlag(AFEINTC_1, AFEINTSRC_SINC2RDY) == bFALSE);
    AD5940_INTCClrFlag(AFEINTSRC_SINC2RDY);
    result = AD5940_ReadAfeResult(AFERESULT_SINC2);
#else
    // Timeout polling method
    int32_t time_out = 1000;
    result = AD5940_TakeMeasurement(AFECTRL_ADCPWR | AFECTRL_ADCCNV, AFEINTSRC_SINC2RDY, AFERESULT_SINC2, &time_out);
#endif    
    digitalWrite(D5, LOW);

    return result;
}

/**
 * @brief Reads an analog voltage on a given pin of the host microcontroller.
 * @param pin The target GPIO analog read pin.
 * @return The calculated voltage in millivolts.
 */
float RP2040_MeasurePin(int pin)
{
    uint16_t value = analogRead(pin);
    float mV = ADC_AVDD * value / ADCFullValue;
    return mV;
}

/**
 * @brief Verifies cell potential using microcontroller ADC resources.
 * @return The potential difference (V_re - V_we) in millivolts.
 */
float RP2040_MeasureOCP()
{
    float wemV = RP2040_MeasurePin(WEpin);
    float remV = RP2040_MeasurePin(REpin);
    return remV - wemV; // Transmits Vre - Vwe according to specifications
}

/**
 * @brief Format and transmit OCP metrics to the host interface over serial.
 * @param we Working Electrode baseline voltage.
 * @param ocp1 OCP potential evaluated from AD5941.
 * @param ocpMeasured OCP potential verified by the host microcontroller ADC.
 */
void PrintOCP(float we, float ocp1, float ocpMeasured)
{
    if (SeeedStatMode)
    {
        // Transmit comma-separated ASCII if in SeeedStat mode
        char buffer[500];
        sprintf(buffer, "%04.0f,%05.1f,%03.1f", we, ocp1, ocpMeasured);
        Serial.println(buffer);
    }
    else
    {
        // Transmit binary packages for high speed processing (4 bytes per float)
        Serial.write((uint8_t*)&we, 4);
        Serial.write((uint8_t*)&ocp1, 4);
        Serial.write((uint8_t*)&ocpMeasured, 4);
    }
}

/**
 * @brief Coordinates the OCP sampling loop, reading matrix paths and executing sweeps.
 */
void Do_AD5941_OCP_Measurement()
{
    // Read the current state of matrix switch configurations for diagnostic purposes
    uint32_t dsw = AD5940_ReadReg(REG_AFE_DSWFULLCON);
    uint32_t psw = AD5940_ReadReg(REG_AFE_PSWFULLCON);
    uint32_t nsw = AD5940_ReadReg(REG_AFE_NSWFULLCON);
    uint32_t tsw = AD5940_ReadReg(REG_AFE_TSWFULLCON);
    
    Log(0x80, __LINE__, "REG_AFE_SWCON=0x%lX Dswitch=0x%lX Nswitch=0x%lX Pswitch=0x%lX Tswitch=0x%lX ",
          AD5940_ReadReg(REG_AFE_SWCON), dsw, nsw, psw, tsw);

    AD5940_AFECtrlS(AFECTRL_ADCPWR | AFECTRL_ADCCNV, bTRUE);  // Power-up ADC and begin conversions
    delay(5);

    digitalWrite(D4, HIGH);
    if (ocpCalibrationCycling == bTRUE)
    {
        // Execute dynamic OCP calibration across a specified potential sweep range
        float step = WEfrom < WEto ? abs(WEstep) : -abs(WEstep);
        for (float we = WEfrom; WEfrom < WEto ? we <= WEto : we >= WEto; we += step)
        {
            delayMicroseconds(500);
            digitalWrite(D4, HIGH);

            SetDACLevel(we); // Apply current step potential
            
            uint32_t ocp_sum = 0;
            float ocpMeasured = 0.0;
            for(uint16_t i = 0; i < OCP_npts; i++)
            {
                ocp_sum += Do1_AD5941_OCP_Measurement();
                ocpMeasured += RP2040_MeasureOCP();
            }

            // Print averaged result
            PrintOCP(we, -(static_cast<float>(ocp_sum) / OCP_npts), ocpMeasured / OCP_npts);
            digitalWrite(D4, LOW);
        }
    }
    else
    {
        // Simple non-sweeping static OCP read
        OCP_sum = 0;
        for(uint16_t i = 0; i < OCP_npts; i++)
        {
            delayMicroseconds(500);
            OCP_sum += Do1_AD5941_OCP_Measurement();
        }
    }
        
    AD5940_AFECtrlS(AFECTRL_ADCPWR | AFECTRL_ADCCNV, bFALSE);   // Power-down ADC conversion cycles
    digitalWrite(D4, LOW);

    ADCCON = AD5940_ReadReg(REG_AFE_ADCCON); // Store status configuration register
    
    if (ocpCalibrationCycling != bTRUE)
    {
        Serial.print('Z'); // Transmit completion token
    }
    ocpCalibrationCycling = bFALSE;
    SeeedStatMode = false;
}

/**
 * @brief Resolves raw ADC counts into cell potential in millivolts.
 * @return The final evaluated OCP value in mV.
 */
float CalculateOCP()
{
    float ocp_mV;
    float ocp_1 = static_cast<float>(OCP_sum) / OCP_npts;
    if (SeeedStatMode)
    {
        ocp_1 *= -1.0;    // Adjust direction to report Vre - Vwe instead of Vwe - Vre
    }
    
    if (useConstAB)
    {
        // Evaluates OCP using linear equations derived during multi-point calibrations
        float ocp_npts = static_cast<float>(OCP_npts);
        ocp_mV = (ocp_1 - constA) / constB;

        Log(0x80, __LINE__, "ocp_1=%.2f constA=%.3f constB=%.3f ocp_mV=%.6f ",
                             ocp_1,     constA,     constB,     ocp_mV);
    }
    else
    {
        // Standard physical calculation based on internal ADC reference parameters
        uint8_t GNPGA = (ADCCON & BITM_AFE_ADCCON_GNPGA) >> BITP_AFE_ADCCON_GNPGA;
        float Vref_mV = (GNPGA == 1) ? 1835.0 : 1820.0;       // Vref varies depending on PGA stage gain selections
        float PGA_G_values[] = { 1.0, 1.5, 2.0, 4.0, 9.0, 9.0 };
        float PGA_G = PGA_G_values[GNPGA];                    

        uint8_t MUXSELP = (ADCCON & BITM_AFE_ADCCON_MUXSELP) >> BITP_AFE_ADCCON_MUXSELP;    
        uint8_t MUXSELN = (ADCCON & BITM_AFE_ADCCON_MUXSELN) >> BITP_AFE_ADCCON_MUXSELN;    

        float ocp_npts = static_cast<float>(OCP_npts);
        const uint8_t MUXSELN_VBIAS_CAP = 8;                                                
        float VBIAS_CAP_mV = (MUXSELN == MUXSELN_VBIAS_CAP) ? 1110.0 : 0.0;                 
        const float ADC_MIDDLE_AND_ACTIVE_RANGE = 1 << 15; // 15-bit offset representing midscale center
        
        // Final standard physical equation conversion
        ocp_mV = (ocp_1 - ADC_MIDDLE_AND_ACTIVE_RANGE) * (Vref_mV / PGA_G) / ADC_MIDDLE_AND_ACTIVE_RANGE + VBIAS_CAP_mV;

        Log(0x80, __LINE__, "ADCCON=0x%X GNPGA=%i MUXSELP=0x%X MUXSELN=0x%X VBIAS_CAP=%.2f Vref_mV=%.0f PGA_G=%.1f ocp_1=%.2f ocp_mV=%.6f ",
                             ADCCON,     GNPGA,   MUXSELP,     MUXSELN,   VBIAS_CAP_mV/1000, Vref_mV,   PGA_G,     ocp_1,     ocp_mV);
    }
    
    Log(0x100, __LINE__, "WEmV=%.0f HSDACDAT=%d ocp_1=%.2f ocp_mV=%.3f ",
                          WEmV,     HSDACDAT,   ocp_1,     ocp_mV);
    return ocp_mV;
}

/**
 * @brief Configures Low-Power Loop parameters (LP DAC, LP TIA routing, bias).
 */
void Config_LPLOOP()
{
    LPDACCfg_Type lpdac_cfg;
    lpdac_cfg.LpdacSel = LPDAC0;
    lpdac_cfg.LpDacVbiasMux = LPDACVBIAS_12BIT; /* LP DAC 12-bit channel sets Reference Electrode bias */
    lpdac_cfg.LpDacVzeroMux = LPDACVZERO_6BIT;  /* LP DAC 6-bit channel sets Working Electrode potential */
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
void Calibrate_HSDAC(uint8_t power_mode, uint8_t pga_g)
{
    HSDACCal_Type hsdac_cal;
    ADCPGACal_Type adcpga_cal;
    CLKCfg_Type local_clk_cfg;

    AD5940_AFEPwrBW(power_mode, AFEBW_250KHZ);

    // Set clock registers to ensure calibration happens at stable reference speeds
    local_clk_cfg.ADCClkDiv = ADCCLKDIV_1;
    local_clk_cfg.ADCCLkSrc = ADCCLKSRC_HFOSC;
    local_clk_cfg.SysClkDiv = SYSCLKDIV_1;
    local_clk_cfg.SysClkSrc = SYSCLKSRC_HFOSC;
    local_clk_cfg.HfOSC32MHzMode = bTRUE;
    local_clk_cfg.HFOSCEn = bTRUE;
    local_clk_cfg.HFXTALEn = bFALSE;
    local_clk_cfg.LFOSCEn = bTRUE;
    AD5940_CLKCfg(&local_clk_cfg);

    // Also mark the global clk_cfg's oscillator as enabled so that the
    // per-frequency HfOSC32MHzMode switch in eisScan() actually takes effect
    // (AD5940_CLKCfg only applies HfOSC32MHzMode when HFOSCEn == bTRUE).
    clk_cfg.HFOSCEn = bTRUE;

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
 * @brief Checks if AD5941's data FIFO has reached threshold to determine if it is busy.
 * @return True if computing.
 */
bool IsAD5940Calculating()
{
    return !(AD5940_INTCTestFlag(AFEINTC_0, AFEINTSRC_DATAFIFOTHRESH));
}

/**
 * @brief Executes Electrochemical Impedance Spectroscopy (EIS) frequency sweeps.
 * @param eisMode 0 = Rz (working cell), 1 = Rcal (reference resistor).
 */
void eisScan(uint8_t eisMode)
{
    Utils_SetStatusLed(WHITE); // Display white during calibration
    Calibrate_HSDAC(0, pga_gain);

#if USE_250202_Version
    // Set safety limits for ADC range errors
    AD5940_WriteReg(REG_AFE_ADCMAX, 61440);  
    AD5940_WriteReg(REG_AFE_ADCMAXSMEN, 61184);
#endif

    if(eisMode == 0) Utils_SetStatusLed(BLUE);    // Blue signals working cell impedance sweeps
    else Utils_SetStatusLed(RED);                 // Red signals internal resistor calibrations

    // Calculate logarithmic frequency steps
    float flo = (float) freqlo / 1000.0;
    float fhi = (float) freqhi / 1000.0;

    float log_start = log10(flo);
    float log_end = log10(fhi);
    float log_step = (log_end - log_start) / (nfreqs - 1);

    for(uint16_t i = 0; i < nfreqs; i++)
    {
        // Re-initialize driver memory blocks to prevent settings drift across steps
        AD5941_InitAll();

        Config_LPLOOP(); // Apply low-power cell bias settings

        float frequency = pow(10, log_start + i * log_step);
        Do_WaveGen(eisMode, frequency, amplitude, tia_rf);  

        // Configure power mode and oscillators based on frequency thresholds
        if(frequency > 80000)
        {
            // High-speed excitation settings
            HpLoopCfg.HsDacCfg.ExcitBufGain = EXCITBUFGAIN_2;  
            HpLoopCfg.HsDacCfg.HsDacGain = HSDACGAIN_1;  
            HpLoopCfg.HsDacCfg.HsDacUpdateRate = 0x07;
            AD5940_HSLoopCfgS(&HpLoopCfg);

            clk_cfg.HfOSC32MHzMode = bTRUE;
            AD5940_CLKCfg(&clk_cfg);

            AD5940_HPModeEn(bTRUE); // Run HFOSC in 32MHz mode
        }
        else
        {
            // Standard frequency excitation settings
            HpLoopCfg.HsDacCfg.ExcitBufGain = EXCITBUFGAIN_2;  
            HpLoopCfg.HsDacCfg.HsDacGain = HSDACGAIN_1;  
            HpLoopCfg.HsDacCfg.HsDacUpdateRate = 0x1B;
            AD5940_HSLoopCfgS(&HpLoopCfg);

            clk_cfg.HfOSC32MHzMode = bFALSE;
            AD5940_CLKCfg(&clk_cfg);

            AD5940_HPModeEn(bFALSE); // Run HFOSC in 16MHz mode
        }

        init_AD5940_ADC(frequency); // Configure ADC filters for the current frequency step

        if (SeeedStatMode)
        {
            digitalWrite(D4, HIGH); // Pulse D4 high to show start of stabilization delay
        }

        // Apply selected settling delays
        if(settling_parameter > 1)
        {
            Delay(settling_parameter, 16, 0, 16);
        }
        else if(settling_parameter == 1)
        {
            if(frequency < 100.0) settling_delay_ms = (uint16_t)(1000/frequency);
            else settling_delay_ms = 10; 

            Delay(settling_delay_ms, 16, 0, 16);
        }

        if (SeeedStatMode)
        {
            digitalWrite(D4, LOW);  // Pulse D4 low to show end of stabilization delay
        }

        // Power up the ADC converters
        AD5940_AFECtrlS(AFECTRL_ADCCNV | AFECTRL_DFT, bFALSE);
        AD5940_AFECtrlS(AFECTRL_ADCPWR, bTRUE); 

        if (adc_delay_ms)
        {
            Delay(adc_delay_ms, -16, 0, -16); 
        }

        AD5940_INTCCfg(AFEINTC_0, AFEINTSRC_ALLINT, bTRUE); 
        AD5940_INTCClrFlag(AFEINTSRC_ALLINT);

        digitalWrite(D4, HIGH);
        AD5940_AFECtrlS(AFECTRL_ADCCNV | AFECTRL_DFT, bTRUE);  // Start conversions and DFT engine

        Delay(&IsAD5940Calculating, 16, 0, 16); // Wait for conversion values to stabilize in FIFO

        AD5940_AFECtrlS(AFECTRL_ADCCNV | AFECTRL_DFT, bFALSE); // Shut down conversions

        // Read real and imaginary components directly from the FIFO read register
        uint32_t DFTREAL, DFTIMAG;
        DFTREAL = AD5940_ReadReg(REG_AFE_DATAFIFORD);
        DFTIMAG = AD5940_ReadReg(REG_AFE_DATAFIFORD);
        digitalWrite(D4, LOW);

        AddMeasurementToHistory(DFTREAL, DFTIMAG);

        // Check if values saturated ADC limits; trigger D5 if error occurred
        if(AD5940_INTCTestFlag(AFEINTC_0, AFEINTSRC_ADCMINERR | AFEINTSRC_ADCMAXERR))
        {
            OutputPulse(D5, 10);
        }

        AD5940_INTCClrFlag(AFEINTSRC_ALLINT);  

        if (SeeedStatMode)
        {
            float real = ToFloat(DFTREAL);
            float imag = ToFloat(DFTIMAG);
            Log(0x20, __LINE__, "eisMode=%i frequency=%.2f DFTREAL=0x%X DFTIMAG=0x%X real=%.2f imag=%.2f ",
                                 eisMode,   frequency, DFTREAL & 0x3ffff, DFTIMAG & 0x3ffff, real, imag);

            if (verbose & 1)
            {
                Serial.print(i + 1);
                Serial.print("=");

                Serial.print(real);
                Serial.print(",");
                Serial.print(imag);
                Serial.println(",");
            }

            AppendMeasurement(real, imag); // Load into buffer
        }
        else
        {
            // Binary streaming write
            Serial.write((uint8_t*)&DFTREAL, 4);
            Serial.write((uint8_t*)&DFTIMAG, 4);
        }
    }

    Utils_SetStatusLed(GREEN);     // Green signals measurement is successfully completed
    Utils_SetStatusPixels(0, 255, 0);
}

/**
 * @brief Coordinates the SeeedStat compatibility scan routine.
 */
void SeeedStatScan()
{
    AD5940_PGA_Calibration();

    ResetMeasurementBuffer();
    eisScan(EIS_mode = 0); // Scan cell impedance

    ResetMeasurementBuffer();
    eisScan(EIS_mode = 1); // Scan reference resistor calibration values

    CalculateNyquistCurve(); // Calculate impedance values
}

/**
 * @brief Helper function to compute magnitude and phase angle from real and imaginary data.
 */
void CalculateMagAndPhase(float* pRealAndImag, float& mag, float& phase)
{
    float real = *pRealAndImag++;
    float imag = *pRealAndImag;

    mag = sqrt(real * real + imag * imag);
    phase = atan2(-imag, real);
}

/**
 * @brief Transforms cell and reference resistor vectors into Nyquist coordinates.
 */
void CalculateNyquistPoint(float* pRZ, float* pRCAL)
{
    float RZmag, RZphase;
    CalculateMagAndPhase(pRZ, RZmag, RZphase);

    float RCALmag, RCALphase;
    CalculateMagAndPhase(pRCAL, RCALmag, RCALphase);

    // Scale unknown impedance magnitudes relative to calibrated RCAL
    float ZUnknownMag = abs(RCALmag / RZmag) * fRcal;
    float ZUnknownPhase = RZphase - RCALphase;

    // Convert polar coordinates to Cartesian coordinates
    float nyquistPointX = ZUnknownMag * cos(ZUnknownPhase);
    float nyquistPointY = ZUnknownMag * sin(ZUnknownPhase);

    Log(0x20, __LINE__, "RZmag=%.2f RZphase=%.2f", RZmag, RZphase);
    Log(0x20, __LINE__, "RCALmag=%.2f RCALphase=%.2f", RCALmag, RCALphase);
    Log(0x20, __LINE__, "ZUnknownMag=%.2f ZUnknownPhase=%.2f", ZUnknownMag, ZUnknownPhase);
    Log(0x20, __LINE__, "nyquistPointX=%.2f nyquistPointY=%.2f", nyquistPointX, nyquistPointY);

    // Stream coordinates back to the host UI
    char buf[100];
    sprintf(buf, "%.3f,%.3f,", nyquistPointX, nyquistPointY);
    Serial.println(buf);
    Serial.flush();
    delay(1);
}

/**
 * @brief Steps through the arrays of cell (Rz) and reference (Rcal) scans to calculate Nyquist values.
 */
void CalculateNyquistCurve()
{
    float* pRZ = Measurements;                              // Cell measurements
    float* pRCAL = &Measurements[2 * numberOfMeasurements]; // Reference measurements
    for (int i = 0; i < numberOfMeasurements; ++i)
    {
        CalculateNyquistPoint(pRZ, pRCAL);
        pRZ += 2;                                           
        pRCAL += 2;                                         
    }
}

/**
 * @brief Setup routine: Core processor startup and structures cleanings.
 */
void setup(void)
{
    // Initialize structures
    AD5940_StructInit(&HpLoopCfg, sizeof(HSLoopCfg_Type));
    AD5940_StructInit(&adc_filter, sizeof(ADCFilterCfg_Type));
    AD5940_StructInit(&adc_base, sizeof(ADCBaseCfg_Type));
    AD5940_StructInit(&DftCfg, sizeof(DFTCfg_Type));
    AD5940_StructInit(&fifo_cfg, sizeof(FIFOCfg_Type));
    AD5940_StructInit(&clk_cfg, sizeof(CLKCfg_Type));
    AD5940_StructInit(&clks_cal, sizeof(ClksCalInfo_Type));

    // Boot modular layers
    g_Data.Begin();     // Initialize state storage structures
    g_Setup.Begin();    // Reset the SPI channels and initialize pins
    g_Comm.Begin(1000000, &g_Data); // Initialize Serial communication at 1 MHz

    // Sync parameters from floats to binary representations
    amplitude = C_Communication::ConvertFloatAmplitudeToUint16(fAmplitude);
    vbias = C_Communication::ConvertFloatBiasToUint16(fBias);
    offset = C_Communication::ConvertFloatOffsetToUint16(fOffset);
    ResetMeasurementBuffer();
}

/**
 * @brief Polling loop: Delegates execution to the modular communication layer command checker.
 */
void loop()
{
    g_Comm.ReadAndProcess(); // Check for incoming serial commands
}
