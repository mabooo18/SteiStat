// D. Bill (adapted from Analog Devices example code)
// Core runner file for orchestrating Cyclic Voltammetry (CV) sweeps using the AD5940/AD5941 hardware sequencer.

#include "Arduino.h"
#include "debug.h"
#include "ad5940.h"
#include "RampTest.h"

#include <LibPrintf.h> // Allows C-style printf formatting in serial outputs
#define PLOT_DATA      /* Output data format compatible with Python UI plotting stream */
#define APPBUFF_SIZE 1024

// Changes LED colors progressively as sweep potential changes
extern void ChangePixelsColor(int16_t& red, int16_t redInc, int16_t& green, int16_t greenInc, int16_t& blue, int16_t blueInc);

// Calibrated internal resistor (RCAL) value
extern float fRcal;

static uint32_t _AppBuff[APPBUFF_SIZE]; // Local SRAM buffer to cache raw AD5940 measurement samples
static float _LFOSCFreq;                // Calibrated Low-Frequency Oscillator frequency

// Default parameters for Cyclic Voltammetry (CV)
float V_start = -200.0;
float V_stop = 600.0;
float Estep = 2.0;          /**< Potential difference between steps (mV) -> determines step count. */
float ScanRate = 100.0;     /**< Voltammetry sweep speed (mV/s) -> determines duration. */
uint16_t CycleNumber = 2;   /**< Number of CV potential scan cycles to perform */

/**
 * @brief Formats and transmits measured potential-current pairs over serial.
 * @param pData Buffer containing voltage and current pairs [V1, I1, V2, I2, ...].
 * @param DataCount Number of float elements in the buffer.
 * @return Returns 0 on success.
 */
static int32_t RampShowResult(float* pData, uint32_t DataCount)
{
    // Steps through pairs and prints them as comma-separated ASCII coordinates
    for (unsigned i = 0; i < DataCount; i += 2)
    {
        Serial.print("CV,");
        Serial.print(pData[i]);
        Serial.print(",");
        Serial.print(pData[i + 1]);
        Serial.println(",");
    }
    return 0;
}

/**
 * @brief Configures platform clocking, sequencer FIFO divisions, and interrupts.
 * @return Returns 0 on success.
 */
static int32_t AD5940PlatformCfg(void)
{
    CLKCfg_Type clk_cfg;
    SEQCfg_Type seq_cfg;
    FIFOCfg_Type fifo_cfg;
    AGPIOCfg_Type gpio_cfg;
    LFOSCMeasure_Type LfoscMeasure;

    // Trigger a hardware reset to clear any hanging states in the AFE
    AD5940_HWReset();
    AD5940_Initialize();

    // Step 1: Configure internal clocks: Enable high-frequency oscillator (HFOSC) at 16MHz
    clk_cfg.HFOSCEn = bTRUE;
    clk_cfg.HFXTALEn = bFALSE;
    clk_cfg.LFOSCEn = bTRUE;
    clk_cfg.HfOSC32MHzMode = bFALSE;
    clk_cfg.SysClkSrc = SYSCLKSRC_HFOSC;
    clk_cfg.SysClkDiv = SYSCLKDIV_1;
    clk_cfg.ADCCLkSrc = ADCCLKSRC_HFOSC;
    clk_cfg.ADCClkDiv = ADCCLKDIV_1;
    AD5940_CLKCfg(&clk_cfg);

    // Step 2: Configure FIFO and Sequencer memory boundaries
    fifo_cfg.FIFOEn = bTRUE;
    fifo_cfg.FIFOMode = FIFOMODE_FIFO;
    fifo_cfg.FIFOSize = FIFOSIZE_2KB;      // Allocate 2 kB for measurement data buffer
    fifo_cfg.FIFOSrc = FIFOSRC_SINC3;       // Fetch raw samples from the Sinc3 filter
    fifo_cfg.FIFOThresh = 4;                // Trigger interrupt when 4 elements are ready
    AD5940_FIFOCfg(&fifo_cfg);

    seq_cfg.SeqMemSize = SEQMEMSIZE_4KB;    // Allocate remaining 4 kB for command sequences
    seq_cfg.SeqBreakEn = bFALSE;
    seq_cfg.SeqIgnoreEn = bTRUE;
    seq_cfg.SeqCntCRCClr = bTRUE;
    seq_cfg.SeqEnable = bFALSE;
    seq_cfg.SeqWrTimer = 0;
    AD5940_SEQCfg(&seq_cfg);

    // Step 3: Configure Interrupt Controllers
    // Configure INTC1 to latch all AFE interrupts for polling
    AD5940_INTCCfg(AFEINTC_1, AFEINTSRC_ALLINT, bTRUE);
    AD5940_INTCClrFlag(AFEINTSRC_ALLINT);
    // Configure INTC0 to raise microcontroller hardware interrupts on specific events (FIFO full, End of sequence)
    AD5940_INTCCfg(AFEINTC_0, AFEINTSRC_DATAFIFOTHRESH | AFEINTSRC_ENDSEQ | AFEINTSRC_CUSTOMINT0 | AFEINTSRC_CUSTOMINT1 | AFEINTSRC_GPT1INT_TRYBRK | AFEINTSRC_DATAFIFOOF, bTRUE);
    AD5940_INTCClrFlag(AFEINTSRC_ALLINT);

    // Step 4: Configure GPIO output mappings
    gpio_cfg.FuncSet = GP0_INT | GP1_GPIO | GP2_SYNC;
    gpio_cfg.InputEnSet = 0;
    gpio_cfg.OutputEnSet = AGPIO_Pin0 | AGPIO_Pin1 | AGPIO_Pin2;
    gpio_cfg.OutVal = AGPIO_Pin1; // Set GP1 high to turn off status LED by default
    gpio_cfg.PullEnSet = 0;
    AD5940_AGPIOCfg(&gpio_cfg);

    // Step 5: Calibrate the low-frequency oscillator (LFOSC) relative to the 16MHz system reference
    LfoscMeasure.CalDuration = 1000.0; // Perform 1-second calibration window
    LfoscMeasure.CalSeqAddr = 0;
    LfoscMeasure.SystemClkFreq = 16000000.0f;
    AD5940_LFOSCMeasure(&LfoscMeasure, &_LFOSCFreq);
    
    AD5940_SleepKeyCtrlS(SLPKEY_UNLOCK);
    return 0;
}

/**
 * @brief Initializes the voltammetry configuration structure with target sweep parameters.
 */
static void _AD5940RampStructInit(void)
{
    AppRAMPCfg_Type* pRampCfg;
    AppRAMPGetCfg(&pRampCfg);

    // Configure memory partition addresses
    pRampCfg->SeqStartAddr = 0x10;      // Reserve first 16 instructions for clock calibration
    pRampCfg->MaxSeqLen = 1024 - 0x10;  // Maximum command list instructions length
    pRampCfg->RcalVal = fRcal;          // Store baseline RCAL resistance
    pRampCfg->ADCRefVolt = 1820.0f;     // Absolute reference potential measured on pin C3
    pRampCfg->FifoThresh = 100;
    pRampCfg->SysClkFreq = 16000000.0f; // 16 MHz reference speed
    pRampCfg->LFOSCClkFreq = _LFOSCFreq;// Set measured LFOSC frequency

    // Configure limits for WE potential bias tuning (Vzero)
    pRampCfg->VzeroLimitHigh = 2400;    // Upper boundary 2.4V
    pRampCfg->VzeroLimitLow = 200;      // Lower boundary 0.2V
    
    // Set step, rates and cycles parameters
    pRampCfg->Estep = Estep;
    pRampCfg->ScanRate = ScanRate;
    pRampCfg->CycleNumber = CycleNumber;
    pRampCfg->StepNumber = 800;
    pRampCfg->RampDuration = 24 * 1000;
    
    // Configure Sinc filters
    pRampCfg->ADCSinc3Osr = ADCSINC3OSR_4;
    pRampCfg->ADCSinc2Osr = ADCSINC2OSR_667;
    pRampCfg->SampleDelay = 7.0f;       // 7 ms settling time after updating DAC before sampling
    pRampCfg->LpAmpPwrMod = LPAMPPWR_NORM;
    
    // Select TIA feedback and load resistor mappings
    pRampCfg->LPTIARtiaSel = LPTIARTIA_1K; // Imax = 0.9V / 1kOhm = 900 uA
    pRampCfg->LPTIARloadSel = LPTIARLOAD_SHORT;
    pRampCfg->AdcPgaGain = ADCPGA_1P5;
    pRampCfg->bRampOneDir = bFALSE;     // bFALSE performs Cyclic Voltammetry (dual directions); bTRUE performs Linear Sweep Voltammetry
}

/**
 * @brief Primary loop orchestrator that builds, launches, and polls the CV sequencer.
 */
static void _AD5940_Main(void)
{
    ENTER("cv _AD5940_Main");

    uint32_t temp;
    AppRAMPCfg_Type *pRampCfg;

    // Load CV parameters and write command sequence to sequencer memory
    AppRAMPInit(_AppBuff, APPBUFF_SIZE);

#ifdef PLOT_DATA
    // Pre-send sweep parameters to Python client for plot scaling setups
    AppRAMPGetCfg(&pRampCfg);
    int Sinc3OSR[] = {2, 4, 5};
    int Sinc2OSR[] = {22, 44, 89, 178, 267, 533, 640, 667, 800, 889, 1067, 1333};
    uint8_t byteData[30];
    byteData[0] = ((int16_t)pRampCfg->RampStartVolt & 0xFF00) >> 8;
    byteData[1] = (int16_t)pRampCfg->RampStartVolt & 0x00FF;
    byteData[2] = ((int16_t)pRampCfg->RampPeakVolt1 & 0xFF00) >> 8;
    byteData[3] = (int16_t)pRampCfg->RampPeakVolt1 & 0x00FF;
    byteData[4] = ((int16_t)pRampCfg->RampPeakVolt2 & 0xFF00) >> 8;
    byteData[5] = (int16_t)pRampCfg->RampPeakVolt2 & 0x00FF;
    byteData[6] = ((int16_t)pRampCfg->Estep & 0xFF00) >> 8;
    byteData[7] = (int16_t)pRampCfg->Estep & 0x00FF;
    byteData[8] = ((int16_t)pRampCfg->ScanRate & 0xFF00) >> 8;
    byteData[9] = (int16_t)pRampCfg->ScanRate & 0x00FF;
    byteData[10] = ((int16_t)pRampCfg->CycleNumber & 0xFF00) >> 8;
    byteData[11] = (int16_t)pRampCfg->CycleNumber & 0x00FF;
    byteData[12] = ((int16_t)Sinc3OSR[pRampCfg->ADCSinc3Osr] & 0xFF00) >> 8;
    byteData[13] = (int16_t)Sinc3OSR[pRampCfg->ADCSinc3Osr] & 0x00FF;
    byteData[14] = ((int16_t)Sinc2OSR[pRampCfg->ADCSinc2Osr] & 0xFF00) >> 8;
    byteData[15] = (int16_t)Sinc2OSR[pRampCfg->ADCSinc2Osr] & 0x00FF;
    byteData[16] = ((int16_t)pRampCfg->StepNumber & 0xFF00) >> 8;
    byteData[17] = (int16_t)pRampCfg->StepNumber & 0x00FF;
    
    // Hold execution briefly to let client UI clear and establish serial buffers
    delay(3000);
#endif

    // Set sequencer control register to start the sweep
    AppRAMPCtrl(APPCTRL_START, 0);
    BlinkLed();
    
    int16_t red = 0;
    int16_t green = 0;
    int16_t blue = 0;

    // Loop polling interrupts until the sequencer asserts the finish flag
    while (!pRampCfg->bTestFinished)
    {
        AppRAMPGetCfg(&pRampCfg);
        if (AD5940_GetMCUIntFlag()) // Check if chip interrupt pin GP0 is low
        {
            AD5940_ClrMCUIntFlag();
            temp = APPBUFF_SIZE;
            
            // Execute Interrupt Service Routine: read FIFO registers and average samples
            AppRAMPISR(_AppBuff, &temp); 
            
            // Print coordinates if a complete voltage step measurements have finished
            if (temp > 0)
            {
                RampShowResult((float *)_AppBuff, temp);
            }
	
            BlinkLed();
            ChangePixelsColor(red, 4, green, 0, blue, -4); // Shift LED color to reflect potential scan
#if DEBUG
            Trace::FlushLogBuffer();
#endif
        }
	
#if MEASURE
        Measure();
#endif
    }
       
    // Reset test finished status state to enable subsequent scans
    pRampCfg->bTestFinished = bFALSE;
#if DEBUG
    Trace::FlushLogBuffer();
#endif
    LEAVE;
}

/**
 * @brief External setup wrapper called by the command processor.
 * @param vstart Start potential of the sweep.
 * @param vstop Turnaround/stop potential of the sweep.
 */
void cvSetup(float vstart, float vstop)
{
    AppRAMPCfg_Type *pRampCfg;
    AppRAMPGetCfg (&pRampCfg);

    // Initialize SPI registers, interrupt channels, and reset pin states
    AD5940_MCUResourceInit(0);
    
    // Load default clock distributions
    AD5940PlatformCfg();
    
    // Initialize base structures
    _AD5940RampStructInit();

    // Map starting potentials
    pRampCfg->RampStartVolt = vstart;
    pRampCfg->RampPeakVolt1 = pRampCfg->RampStartVolt;
    pRampCfg->RampPeakVolt2 = vstop;

    // Run the main sweep loop
    _AD5940_Main();
}
