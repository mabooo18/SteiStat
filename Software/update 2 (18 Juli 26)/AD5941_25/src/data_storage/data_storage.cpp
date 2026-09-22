/******************************************************************************
 * @file:    data_storage.cpp
 * @brief:   Implementasi C_DataStorage — Inisialisasi parameter electrochemical
 *****************************************************************************/

#include "data_storage.h"

/**
 * @brief Initializes all operational parameters, calibration constants, limits, 
 *        and buffers to their baseline default configurations on startup.
 */
void C_DataStorage::Begin() {

    // --- System Status and Communication Mode Settings ---
    SystemStatus            = HUNSTAT_WAITING;  // System defaults to idle waiting status
    SeeedStatMode           = false;            // Defaults to raw binary streaming format (false)
    Verbose                 = 0;                // Logs disabled by default

    // --- Electrochemical Impedance Spectroscopy (EIS) parameters ---
    PGA_Gain                = DEFAULT_PGA_GAIN;       // Base amplifier gain code (1 = 1.5x)
    TIA_Rf                  = DEFAULT_TIA_RF;         // TIA feedback resistor index (3 = 10 kOhm)
    EIS_Mode                = MODE_EIS_RZ;            // Start EIS measuring working cell impedance Rz
    NFreqs                  = DEFAULT_NFREQS;         // Number of step frequencies in sweeps (default: 50)
    Amplitude               = DEFAULT_AMPLITUDE;      // AC amplitude DAC code
    VBias                   = DEFAULT_VBIAS;          // Reference voltage bias DAC code
    VZero                   = DEFAULT_VZERO;          // Offset potential bias DAC code (vzero)
    Offset                  = DEFAULT_OFFSET;         // DC offset code
    FreqLo                  = 0;                      // Lower sweep limit in Hz (default: 0)
    FreqHi                  = 0;                      // Upper sweep limit in Hz (default: 0)
    CGMax                   = DEFAULT_CGMAX;          // Maximum TIA Rf * PGA Gain ceiling
    CGMin                   = DEFAULT_CGMIN;          // Minimum combined gain floor
    UseVariableGain         = 0;                      // Disabled auto gain sweeping by default
    fRcal                   = DEFAULT_RCAL;           // RCAL baseline resistor value (default: 10 kOhm)
    fAmplitude              = DEFAULT_FAMPLITUDE;     // AC Amplitude target value in mV (default: 50 mV)
    fBias                   = DEFAULT_FBIAS;          // DC Bias target value in mV (default: 0 mV)
    fOffset                 = DEFAULT_FOFFSET;        // DC Offset target value in mV (default: 0 mV)

    // --- EIS Measurement Result Buffer Arrays ---
    pMeasurement            = Measurements;           // Pointer reference to result array start
    NumberOfMeasurements    = 0;                      // Reset sweep step counter
    memset(Measurements, 0, sizeof(Measurements));    // Clear previous impedance datasets

    // --- Open Circuit Potential (OCP) Parameters ---
    OCP_Npts                = DEFAULT_OCP_NPTS;       // Sample points to average for OCP calculation (default: 10)
    OCP_Sum                 = 0;                      // Sum accumulator variable
    ADCCON                  = 0;                      // Holds cached ADC configuration status register
    ConstA                  = DEFAULT_CONST_A;        // Offset coefficient A for linear OCP equations
    ConstB                  = DEFAULT_CONST_B;        // Slope coefficient B for linear OCP equations
    UseConstAB              = false;                  // Use physical ref math instead of const equations by default
    WE_mV                   = 0.0f;                   // Target WE potential applied during static calibrations
    WEFrom_mV               = 0.0f;                   // Start sweep target potential
    WETo_mV                 = 0.0f;                   // Stop sweep target potential
    WEStep_mV               = 0.0f;                   // Step potential size
    OCP_Calibration         = false;                  // Set true to activate calibration loop routines
    OCP_CalibrationCycling  = false;                  // Set true to perform sweeps of OCP calibrations
    HSDACDAT                = 0;                      // High speed DAC MMR configuration register buffer

    // --- Cyclic Voltammetry (CV) Parameters ---
    V_Start                 = 0.0f;                   // Start potential of potential scans in mV
    V_Stop                  = 0.0f;                   // End potential of scans in mV
    EStep                   = 0.0f;                   // Staircase step size in mV
    ScanRate                = 0.0f;                   // Sweep velocity in mV/s
    CycleNumber             = 1;                      // Perform 1 CV cycle by default

    // --- Chronoamperometry (CA) Parameters ---
    CA_Voltage_mV           = DEFAULT_CA_VOLTAGE_MV;  // Target step potential applied to cell in mV (default: 0 mV)
    CA_Duration_s           = DEFAULT_CA_DURATION_S;  // Measurement run time in seconds (default: 1 s)
    CA_SampleRate_Hz        = DEFAULT_CA_SAMPLERATE;  // Sampling frequency in Hz (default: 100 Hz)
    CA_NumSamples           = 0;                      // Total data points to record

    // --- Square Wave Voltammetry (SWV) Parameters ---
    SWV_Start_mV            = DEFAULT_SWV_START;      // Start scan potential in mV (default: -100 mV)
    SWV_End_mV              = DEFAULT_SWV_END;        // End scan potential in mV (default: 100 mV)
    SWV_Step_mV             = DEFAULT_SWV_STEP;       // Staircase potential increment size in mV (default: 5 mV)
    SWV_Amplitude_mV        = DEFAULT_SWV_AMPLITUDE;  // Square pulse amplitude overlaid in mV (default: 25 mV)
    SWV_Frequency_Hz        = DEFAULT_SWV_FREQUENCY;  // Operating pulse frequency (default: 50 Hz)
    SWV_SampleDelay_s       = DEFAULT_SWV_SAMPLEDELAY;// Wait time before sampling current in seconds (default: 0.02 s)

    // --- Differential Pulse Voltammetry (DPV) Parameters ---
    DPV_Start_mV            = DEFAULT_DPV_START;      // Start scan potential in mV (default: -100 mV)
    DPV_End_mV              = DEFAULT_DPV_END;        // End scan potential in mV (default: 100 mV)
    DPV_Step_mV             = DEFAULT_DPV_STEP;       // Staircase potential increment size in mV (default: 5 mV)
    DPV_Amplitude_mV        = DEFAULT_DPV_AMPLITUDE;  // Pulse amplitude overlaid in mV (default: 50 mV)
    DPV_PulseWidth_s        = DEFAULT_DPV_PULSEWIDTH; // Pulse duration in seconds (default: 0.05 s)
    DPV_PulsePeriod_s       = DEFAULT_DPV_PULSEPERIOD;// Step period duration in seconds (default: 0.2 s)
    DPV_SampleDelay_s       = DEFAULT_DPV_SAMPLEDELAY;// Wait time before sampling current in seconds (default: 0.02 s)
}