// Hardware control module for dynamic config of ADC filters and DFT parameters based on operational measurement frequencies.

#include "adc_control.h"

// Access global configuration structs from the orchestrator
extern ADCFilterCfg_Type adc_filter;
extern DFTCfg_Type DftCfg;
extern FIFOCfg_Type fifo_cfg;
extern ADCBaseCfg_Type adc_base;

/**
 * @brief Configures ADC filter stages and DFT size dynamically depending on target frequency range.
 *        Optimizes sampling rates, averages, and windowing configurations to satisfy Nyquist parameters.
 * @param freq Operating excitation frequency in Hz.
 */
void Hardware_Init_AD5940_ADC(float freq)
{
    // Step 1: Initialize FIFO settings and buffer locations (clearing state first)
    fifo_cfg.FIFOEn = bFALSE;
    fifo_cfg.FIFOMode = FIFOMODE_FIFO;
    fifo_cfg.FIFOSize = FIFOSIZE_4KB;
    fifo_cfg.FIFOSrc = FIFOSRC_DFT;          // Store calculated DFT outputs in FIFO
    fifo_cfg.FIFOThresh = 2;                 // Trigger interrupt when real/imag pair is ready
    AD5940_FIFOCfg(&fifo_cfg);

    fifo_cfg.FIFOEn = bTRUE;
    AD5940_FIFOCfg(&fifo_cfg);

    // Map ADC Multiplexer to measure across the HS TIA inputs
    AD5940_ADCMuxCfgS(ADCMUXP_HSTIA_P, ADCMUXN_HSTIA_N);

    // Reset settings structures
    AD5940_StructInit(&adc_filter, sizeof(adc_filter));
    AD5940_StructInit(&DftCfg, sizeof(DftCfg));

    // Step 2: Determine filter chain and DFT parameter sets based on operating frequency
    if (freq < .11) {
        // Very low frequencies (< 0.11 Hz): High OSR and large DFT window size
        adc_filter.ADCAvgNum = ADCAVGNUM_16;
        adc_filter.ADCSinc2Osr = ADCSINC2OSR_1067;
        adc_filter.ADCSinc3Osr = ADCSINC3OSR_4;
        adc_filter.BpNotch = bTRUE;
        adc_filter.BpSinc3 = bFALSE;
        adc_filter.Sinc2NotchEnable = bTRUE;
        adc_filter.ADCRate = ADCRATE_800KHZ;

        DftCfg.DftNum = DFTNUM_16384;        // 16384-point DFT for high frequency resolution
        DftCfg.DftSrc = DFTSRC_SINC2NOTCH;
        DftCfg.HanWinEn = bTRUE;             // Apply Hanning window to prevent spectral leakage
    }
    else if (freq < .51) {
        // Frequencies between 0.11 Hz and 0.51 Hz
        adc_filter.ADCAvgNum = ADCAVGNUM_16;
        adc_filter.ADCSinc2Osr = ADCSINC2OSR_267;
        adc_filter.ADCSinc3Osr = ADCSINC3OSR_5;
        adc_filter.BpNotch = bTRUE;
        adc_filter.BpSinc3 = bFALSE;
        adc_filter.Sinc2NotchEnable = bTRUE;
        adc_filter.ADCRate = ADCRATE_800KHZ;

        DftCfg.DftNum = DFTNUM_8192;
        DftCfg.DftSrc = DFTSRC_SINC2NOTCH;
        DftCfg.HanWinEn = bTRUE;
    }
    else if (freq < 5) {
        // Frequencies between 0.51 Hz and 5 Hz
        adc_filter.ADCAvgNum = ADCAVGNUM_16;
        adc_filter.ADCSinc2Osr = ADCSINC2OSR_178;
        adc_filter.ADCSinc3Osr = ADCSINC3OSR_4;
        adc_filter.BpNotch = bTRUE;
        adc_filter.BpSinc3 = bFALSE;
        adc_filter.Sinc2NotchEnable = bTRUE;
        adc_filter.ADCRate = ADCRATE_800KHZ;

        DftCfg.DftNum = DFTNUM_8192;
        DftCfg.DftSrc = DFTSRC_SINC2NOTCH;
        DftCfg.HanWinEn = bTRUE;
    }
    else if (freq < 450) {
        // Frequencies between 5 Hz and 450 Hz
        adc_filter.ADCAvgNum = ADCAVGNUM_16;
        adc_filter.ADCSinc2Osr = ADCSINC2OSR_44;
        adc_filter.ADCSinc3Osr = ADCSINC3OSR_4;
        adc_filter.BpNotch = bTRUE;
        adc_filter.BpSinc3 = bFALSE;
        adc_filter.Sinc2NotchEnable = bTRUE;
        adc_filter.ADCRate = ADCRATE_800KHZ;

        DftCfg.DftNum = DFTNUM_4096;
        DftCfg.DftSrc = DFTSRC_SINC2NOTCH;
        DftCfg.HanWinEn = bTRUE;
    }
    else if (freq < 80000) {
        // Frequencies between 450 Hz and 80 kHz: Bypass Sinc2 notch filter (use raw Sinc3 stream)
        adc_filter.ADCAvgNum = ADCAVGNUM_16;
        adc_filter.ADCSinc2Osr = ADCSINC2OSR_178;
        adc_filter.ADCSinc3Osr = ADCSINC3OSR_4;
        adc_filter.BpNotch = bTRUE;
        adc_filter.BpSinc3 = bFALSE;
        adc_filter.Sinc2NotchEnable = bFALSE;
        adc_filter.ADCRate = ADCRATE_800KHZ;

        DftCfg.DftNum = DFTNUM_16384;
        DftCfg.DftSrc = DFTSRC_SINC3;
        DftCfg.HanWinEn = bTRUE;
    }
    else {
        // High frequencies (> 80 kHz)
        adc_filter.ADCAvgNum = ADCAVGNUM_16;
        adc_filter.ADCSinc2Osr = ADCSINC2OSR_178;
        adc_filter.ADCSinc3Osr = ADCSINC3OSR_2;
        adc_filter.BpNotch = bTRUE;
        adc_filter.BpSinc3 = bFALSE;
        adc_filter.Sinc2NotchEnable = bFALSE;
        adc_filter.ADCRate = ADCRATE_800KHZ;

        DftCfg.DftNum = DFTNUM_16384;
        DftCfg.DftSrc = DFTSRC_SINC3;
        DftCfg.HanWinEn = bTRUE;
    }

    // Step 3: Write configuration structures to AD5941 registers
    AD5940_ADCFilterCfgS(&adc_filter);
    AD5940_DFTCfgS(&DftCfg);
    AD5940_ADCBaseCfgS(&adc_base);
}
